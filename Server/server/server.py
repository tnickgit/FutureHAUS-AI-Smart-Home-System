import json
import asyncio
import logging
import sys
from typing import Dict, Any

import websockets
from websockets.legacy.server import WebSocketServerProtocol
from websockets.exceptions import ConnectionClosed

logger = logging.getLogger(__name__)

# Map node_id/MAC -> websocket connection
CLIENTS: Dict[str, WebSocketServerProtocol] = {}
STATE: Dict[str, Any] = {}
STATE_LOCK = asyncio.Lock()
ROOT_WS: WebSocketServerProtocol | None = None
ROOT_ID: str | None = None

rows = 4
columns = 100
database = [[0 for _ in range(columns)] for _ in range(rows)]
db_time = [[0 for _ in range(columns)] for _ in range(rows)]
index = 0

ELEC = 3
WATER = 0
HVAC = 2
LIGHT = 1

WS_PORT = 8765
async def broadcast(obj: dict) -> None:
    if not CLIENTS:
        return
    msg = json.dumps(obj)
    results = await asyncio.gather(*(ws.send(msg) for ws in list(CLIENTS.values())),
                                   return_exceptions=True)
    # drop dead connections
    for (node_id, ws), r in zip(list(CLIENTS.items()), results):
        if isinstance(r, Exception):
            CLIENTS.pop(node_id, None)

def summarize_event(node_id: str, payload: Any) -> dict:
    # payload can be anything; if it's a dict we can check "failed"
    if isinstance(payload, dict) and payload.get("failed") is True:
        return {"type": "event", "severity": "critical",
                "message": f"Failed to get data from {node_id}."}
    return {"type": "event", "severity": "info",
            "message": f"data collected from {node_id}: {payload}"}

# This function will place all critical systems data and time it was recives into the database
def place_data(CS: int, data: Any, time: str) -> None:
    global index
    database[CS][index] = data
    db_time[CS][index] = time
    index = (index + 1) % columns

# this function is used to send any temperture commands if any change is needed
async def handle_temp_data(src_id, data):
    try:
        temp = float(data)
    except (TypeError, ValueError):
        logger.info(f"could not convert the data from {src_id}: {data}")
        return

    if not (temp >= 70 or temp <= 62): #if the temp is not in between cancel
        return

    if ROOT_ID is None:
        logger.info("root not connected")
        return

    target_ws = CLIENTS.get(ROOT_ID)
    if not target_ws:
        logger.info(f"{ROOT_ID} not connected")
        return

    send_data = {
        "target": src_id,
        "cmd": "SET_TEMP",
        "value": 65
    }

    try:
        logger.info(f"sending over the data: {send_data}")
        await target_ws.send(json.dumps(send_data))
    except ConnectionClosed:
        logger.info(f"{src_id} connection has disconnected")
        CLIENTS.pop(src_id, None);
    except Exception as e:
        logger.info("could not send data")

# this function will interpret the JSON file and then place the data accordingly using place data function
async def handle_sensor_data(msg: dict) -> None:
    SRC_ID = msg.get("src_id")
    NODE_TYPE = msg.get("sensor_type")
    TIMESTAMP = msg.get("timestamp")
    DATA = msg.get("data")

    if NODE_TYPE == ELEC:
        place_data(ELEC, DATA, TIMESTAMP)
    elif NODE_TYPE == WATER:
        place_data(WATER, DATA, TIMESTAMP)
    elif NODE_TYPE == HVAC:
        place_data(HVAC, DATA, TIMESTAMP)
        await handle_temp_data(SRC_ID, DATA)
    elif NODE_TYPE == LIGHT:
        place_data(LIGHT, DATA, TIMESTAMP)

    async with STATE_LOCK:
        STATE.setdefault(SRC_ID, {})
        STATE[SRC_ID][NODE_TYPE] = DATA

    # await broadcast(summarize_event(SRC_ID, DATA))

# this function is used to handle any control commands such as HVAC and lightings using the JSON file
async def handle_control_node(ws: WebSocketServerProtocol, msg: dict) -> None:
    SRC_ID = msg.get("src_id")
    TARGET_ID = msg.get("target")
    CMD = msg.get("cmd")
    VALUE = msg.get("value")
    TIMESTAMP = msg.get("timestamp")

    target_ws = CLIENTS.get(TARGET_ID)
    if not target_ws:
        await ws.send(json.dumps({"type": "error", "error": "target_not_connected", "target": TARGET_ID}))
        return

    command_msg = {
        "type": "Control_Node",
        "src_id": SRC_ID,
        "target": TARGET_ID,
        "cmd": CMD,
        "value": VALUE,
        "timestamp": TIMESTAMP,
    }

    await target_ws.send(json.dumps(command_msg))

    data_sent = f"{TIMESTAMP} {CMD} {VALUE}"
    await broadcast(summarize_event(TARGET_ID, data_sent))

async def handle_WSS_command(ws: WebSocketServerProtocol, msg: dict) -> None:
    global ROOT_WS

    TARGET_ID = msg.get("target")
    CMD = msg.get("cmd")
    VALUE = msg.get("value")
    TIME = msg.get("timestamp")

    if ROOT_WS is None:
        await ws.send(json.dumps({"type": "error", "error": "root_not_connected", "target": TARGET_ID}))
        return

    # forward WSS command to root (includes target so root can route)
    command_msg_to_root = {
        "type": "WSS_Command",
        "target": TARGET_ID,
        "cmd": CMD,
        "value": VALUE,
        "timestamp": TIME
    }

    await ROOT_WS.send(json.dumps(command_msg_to_root))
    await ws.send(json.dumps({"type": "ack", "status": "sent_to_root", "target": TARGET_ID}))

async def handle_root_command(ws: WebSocketServerProtocol, msg: dict) -> None:
    # message coming FROM root back to server (ack/status/events)
    CMD = msg.get("cmd")
    VALUE = msg.get("value")
    TIME = msg.get("timestamp")

    event_msg = {
        "type": "event",
        "source": "root",
        "cmd": CMD,
        "value": VALUE,
        "timestamp": TIME
    }
    await broadcast(event_msg)

async def handle_message(ws: WebSocketServerProtocol, msg: dict) -> None:
    mtype = msg.get("node_type")
    # await handle_sensor_data(msg)
    if mtype == "SENSOR":
        await handle_sensor_data(msg)
    # elif mtype == "Control_Node":
    #     await handle_control_node(ws, msg)
    # elif mtype == "WSS_Command":
    #     await handle_WSS_command(ws, msg)
    # elif mtype == "Root_Command":
    #     await handle_root_command(ws, msg)
    # else:
    #     logger.info("handle_message error here")
    #     await ws.send(json.dumps({"type": "error", "error": "unknown_message_type"}))

async def handler(ws: WebSocketServerProtocol) -> None:
    global ROOT_WS, ROOT_ID
    print("new websocket client has arrived")
    node_id: str | None = None

    try:
        async for raw in ws:
            try:
                msg = json.loads(raw) #get the data from the json file sent by the root
            except json.JSONDecodeError:
                logger.info("the error is in json.JSONDecodeError")
                await ws.send(json.dumps({"type": "error", "message": "invalid JSON"}))
                continue

            if not isinstance(msg, dict):
                logger.info("the isInstance file failed")
                await ws.send(json.dumps({"type": "error", "message": "message must be a JSON object"}))
                continue

            # register node if it provides src_id
            if node_id is None and msg.get("src_id"):
                node_id = msg["src_id"]
                CLIENTS[node_id] = ws
                logger.info(f"node id is {node_id}")
                # if your nodes provide role, capture root
                if msg.get("isRoot") == True:
                    ROOT_WS = ws
                    ROOT_ID = node_id
                    logger.info(f"root id is {ROOT_ID} and root ws is {ROOT_WS}")

            await handle_message(ws, msg)
    except ConnectionClosed:
        pass
    except Exception as e:
        print(f"connection error was closed with {node_id} = {e}")
    finally:
        if node_id:
            CLIENTS.pop(node_id, None)
        if ROOT_WS is ws:
            ROOT_WS = None
            ROOT_ID = None

async def main():
    # run the websocket server using the handler function on ip 0.0.0.0 and the port 8765
    logging.basicConfig(stream=sys.stdout, level=logging.INFO)
    async with websockets.serve(handler, "0.0.0.0", 8765):
        print("Running on ws://0.0.0.0:8765")
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())