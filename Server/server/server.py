import json
import asyncio
import logging
import sys
from typing import Dict, Any
from logging.handlers import RotatingFileHandler
# from ai import process_sensor_data

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

rows = 5
columns = 1000
database = [[0 for _ in range(columns)] for _ in range(rows)]
db_time = [[0 for _ in range(columns)] for _ in range(rows)]
index = 0

ELEC = 3
WATER = 0
HVAC = 2
LIGHT = 1
MOTION = 4

port = 8765

# BROADCASTING FUNCTIONS 
# ----------------------------------------------------------------------------------------------------------------------
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

# HELPER FUNCTIONS 
# ----------------------------------------------------------------------------------------------------------------------

# This function will place all critical systems data and time it was recives into the database
def place_data(CS: int, data: Any, time: str) -> None:
    global index
    database[CS][index] = data
    db_time[CS][index] = time
    index = (index + 1) % columns


def get_root_ws() -> WebSocketServerProtocol | None:
    if ROOT_ID is None:
        logger.info("root not connected")
        return None

    target_ws = CLIENTS.get(ROOT_ID)
    if not target_ws:
        logger.info(f"{ROOT_ID} not connected")
        return None

    return target_ws

def give_data_to_ai(data: dict):
    result = process_sensor_data(data)
    logger.info(f"Data has been sent to AI and produced {result}")

# HANDLERS THAT SEND TO BACK TO THE NODE AND AI
# ----------------------------------------------------------------------------------------------------------------------

# this function is used to send any temperture commands if any change is needed
async def handle_temp_data(src_id, data, time):
    try:
        temp = float(data.get("temperature"))
        humidity_percent = float(data.get("humidity_percent"))
        set_temp = float(data.get("set_temperature"))
    except (TypeError, ValueError):
        logger.info(f"could not convert the data from {src_id}: {data}")
        return

    if not (temp >= 70 or temp <= 62):  # if the temp is not in between cancel
        return


    target_ws = get_root_ws()
    if not target_ws:
        return


    # data sent back to the node 
    send_data = {
        "target": src_id,
        "cmd": "SET_TEMP",
        "value": set_temp
    }
    # node sent to the ai
    send_data_ai = {
        "type": "temp_hum",
        "timestamp": time,
        "temperature_f": temp,
        "humidity_percent": humidity_percent
    }

    # this data is sent back to the microcontroller node
    try:
        logger.info(f"At {time} the temp was {temp} and was set to {set_temp}")
        await target_ws.send(json.dumps(send_data))
    except ConnectionClosed:
        logger.info(f"{src_id} connection has disconnected")
        CLIENTS.pop(src_id, None)
    except Exception as e:
        logger.info("could not send data")

    # this sends the data to the ai model and logs what happens
    give_data_to_ai(send_data_ai)

# this function handles data from the water sensor and sends it to the AI when the event when using water says stop
async def handle_light_data(src_id, data, time):

    # data that is sent to the AI
    lux_data = data.get("lux")
    try:
        lux_value = float(lux_data)
    except (TypeError, ValueError):
        logger.info(f"could not convert lux to float from {src_id}: {lux_data}")
        return

    send_data_ai = {
            "type": "light",
            "timestamp": time,
            "lux": float(lux_value)
    }

    isOn = data.get("isOn")
    send_data_client = {}
    if (isOn == True):
        send_data_client = {
            "target": src_id,
            "cmd": "LIGHT_ON",
            "value": isOn
        }
        logger.info(f"Light is on and brightness is set to {lux_data}")
    else:
        send_data_client = {
            "target": src_id,
            "cmd": "LIGHT_OFF",
            "value": isOn
        }
        logger.info(f"light is off")

    # data sent back to the client
    target_ws = get_root_ws()
    if not target_ws:
        return

    try:
        await target_ws.send(json.dumps(send_data_client))
    except ConnectionClosed:
        logger.info(f"{src_id} connection has disconnected")
        CLIENTS.pop(src_id, None)
    except Exception as e:
        logger.info("could not send data")

    # this sends the data to the ai model and logs what happens
    give_data_to_ai(send_data_ai)


# HANDLERS THAT ONLY SEND BACK TO THE AI
# ----------------------------------------------------------------------------------------------------------------------

# This function will gather data from the motion sensor and send to the ai
async def handle_motion_data(src_id, data, time):
    seconds_since_motion = float(data.get("seconds"))
    type = data.get("type")

    send_data_ai = {
        "type": "type",
        "timestamp": "time",
        "seconds_since_motion": seconds_since_motion
    }

    # this sends the data to the ai model and logs what happens
    give_data_to_ai(send_data_ai)

# This function will handle data from water sensor and send over to AI
async def handle_water_data(node_type, src_id, data, time):

    #for water data, data is going to have multiple sub catagories
    fixure = data.get("fixure")
    amount = data.get("amount")
    event = data.get("event")

    if event == "stop":
        logger.info("Water has stopped running. Used {amount} gal/s")
        place_data(node_type, data, time) # put data in database
    elif event == "start":
        logger.info(f"Water is running from {fixure}")

    # make json file for what to send the AI
    send_data_ai = {
        "type": "water_event",
        "timestamp": time,
        "fixture": fixure,
        "event": event,
        "total_gallons": float(amount)
    }


    # this sends the data to the ai model and logs what happens
    give_data_to_ai(send_data_ai)
    

# this functions needs to gather data from the sensor and arrage the JSON file recieved and place it into a JSON
# file that is sent to the AI
async def handle_electric_data(src_id, data, time):
    usage = data.get("usage")
    type = data.get("type")

    send_data_ai = {
        "type": type,
        "timestamp": time,
        "usage": usage
    }

    # this sends the data to the ai model and logs what happens
    give_data_to_ai(send_data_ai)


# MAIN WEBSOCKET DATA MANAGEMENT
# ----------------------------------------------------------------------------------------------------------------------

# this function will interpret the JSON file and then place the data accordingly using place data function
async def handle_sensor_data(msg: dict) -> None:
    SRC_ID = msg.get("src_id")
    NODE_TYPE = msg.get("sensor_type")
    TIMESTAMP = msg.get("timestamp")
    DATA = msg.get("data")

    if NODE_TYPE == ELEC:
        logger.info(f"Electricity: Recieved Data")
        await handle_electric_data(SRC_ID, DATA, TIMESTAMP)
        place_data(ELEC, DATA, TIMESTAMP)

    elif NODE_TYPE == WATER:
        logger.info(f"Water: Recieved Data")
        await handle_water_data(NODE_TYPE, SRC_ID, DATA, TIMESTAMP)

    elif NODE_TYPE == HVAC:
        logger.info(f"Temp: Recieved Data")
        place_data(HVAC, DATA, TIMESTAMP)
        await handle_temp_data(SRC_ID, DATA, TIMESTAMP)

    elif NODE_TYPE == LIGHT:
        logger.info(f"Light: Recieved Data")
        place_data(LIGHT, DATA, TIMESTAMP)
        await handle_light_data(SRC_ID, DATA, TIMESTAMP)

    elif NODE_TYPE == MOTION:
        logger.info(f"Motion Detected")
        await handle_motion_data(SRC_ID, DATA, TIMESTAMP)
        place_data(MOTION, DATA, TIMESTAMP)

    async with STATE_LOCK:
        STATE.setdefault(SRC_ID, {})
        STATE[SRC_ID][NODE_TYPE] = DATA

    # await broadcast(summarize_event(SRC_ID, DATA))


async def handle_message(ws: WebSocketServerProtocol, msg: dict) -> None:
    mtype = msg.get("node_type")
    # await handle_sensor_data(msg)
    if mtype == "SENSOR":
        await handle_sensor_data(msg)


async def handler(ws: WebSocketServerProtocol) -> None:
    global ROOT_WS, ROOT_ID
    logger.info("A new client has joined")
    node_id: str | None = None

    try:
        async for raw in ws:
            try:
                msg = json.loads(raw)  # get the data from the json file sent by the root
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


def setup_logging():
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)

    # prevent duplicate handlers
    if logger.hasHandlers():
        logger.handlers.clear()

    # formatter
    formatter = logging.Formatter(
        "%(asctime)s | %(levelname)s | %(name)s | %(message)s"
    )

    # console handler
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(logging.INFO)
    console_handler.setFormatter(formatter)

    # file handler
    file_handler = RotatingFileHandler(
        "server.log",
        maxBytes=5_000_000,  # 5 MB
        backupCount=3
    )
    file_handler.setLevel(logging.INFO)
    file_handler.setFormatter(formatter)

    # attach handlers
    logger.addHandler(console_handler)
    logger.addHandler(file_handler)


async def main():
    # run the websocket server using the handler function on ip 0.0.0.0 and the port 8765
    # logging.basicConfig(stream=sys.stdout, level=logging.INFO)
    setup_logging()
    async with websockets.serve(handler, host="0.0.0.0", port=port):
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())