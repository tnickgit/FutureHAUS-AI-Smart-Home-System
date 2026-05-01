import json
import asyncio
import logging
import sys
import requests
from typing import Dict, Any
from logging.handlers import RotatingFileHandler
from ai import process_sensor_update, get_water_totals

from datetime import datetime

import websockets
from websockets.legacy.server import WebSocketServerProtocol
from websockets.exceptions import ConnectionClosed
from websockets.exceptions import ConnectionClosedOK, ConnectionClosedError

logger = logging.getLogger(__name__)

# Map node_id/MAC -> websocket connection
CLIENTS: Dict[str, WebSocketServerProtocol] = {}
STATE: Dict[str, Any] = {}
STATE_LOCK = asyncio.Lock()
ROOT_WS: WebSocketServerProtocol | None = None
ROOT_ID: str | None = None

IPAD_SRC = None
IPAD_ID = None

FAN_NODE = None
LIGHT_NODE = None

BULB_IP = "192.168.1.85"
BASE_URL = f"http://{BULB_IP}/light/kauf_bulb"

shelly_ip = "192.168.1.48" 
url = f"http://{shelly_ip}/rpc"

LIGHT_PRE_STATE = "Light_Off"
NO_MOTION_CNT = 0
TURN_OFF_LIGHT_SIGNAL = 3

CURR_BRIGHTNESS = 50

rows = 5
columns = 1000
database = [[0 for _ in range(columns)] for _ in range(rows)]
db_time = [[0 for _ in range(columns)] for _ in range(rows)]
index = 0

MAX_RECORDS = 100000
database_file = "database.json"

ELEC = 3
WATER = 0
HVAC = 2
LIGHT = 1
MOTION = 4

port = 8765

# BROADCASTING FUNCTIONS 
# ----------------------------------------------------------------------------------------------------------------------
async def broadcast(obj: dict[str, Any]) -> None:
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

async def store_json_commands(incoming_data: Any):
    try:
        with open(database_file, "r") as db:
            data = json.load(db)
    except (FileNotFoundError, json.JSONDecodeError):
        data = []

    # append actual object (NOT string)
    data.append(incoming_data)

    # keep only last MAX_RECORDS
    if len(data) > MAX_RECORDS:
        data = data[-MAX_RECORDS:]

    # write proper JSON
    with open(database_file, "w") as db:
        json.dump(data, db, indent=2)


def get_root_ws() -> WebSocketServerProtocol | None:
    if ROOT_ID is None:
        logger.info("root not connected")
        return None

    target_ws = CLIENTS.get(ROOT_ID)
    if not target_ws:
        logger.info(f"{ROOT_ID} not connected")
        return None

    return target_ws

async def give_data_to_ai(data: dict):
    global NO_MOTION_CNT
    result = process_sensor_update(data)
    logger.info(f"Data has been sent to AI and produced {result}")
    if result.get('light') == 'off' and NO_MOTION_CNT >= TURN_OFF_LIGHT_SIGNAL: # change to 120 later
        logger.info("got light is off from ai ")
        send_light_ai_data = {
            "lux": CURR_BRIGHTNESS,
            "isOn": False
        }
        NO_MOTION_CNT = 0
        await handle_light_data(src_id="AI_data", data=send_light_ai_data, time=datetime.now())

# These function will control the lights to the lightbulb itself
def turn_on_light():
    requests.post(f"{BASE_URL}/turn_on")

def turn_off_light():
    requests.post(f"{BASE_URL}/turn_off")

def set_light_brightness(level: int):
    # level 0-255
    real_level = (level / 100) * 255
    requests.post(f"{BASE_URL}/turn_on", params={"brightness": real_level})

def set_color(r: int, g: int, b: int):
    # r, g, b are 0-255
    requests.post(f"{BASE_URL}/turn_on", params={"r": r, "g": g, "b": b})

def set_brightness_and_color(level: int, r: int, g: int, b: int):
    real_level = (level / 100) * 255
    requests.post(f"{BASE_URL}/turn_on", params={"brightness": real_level, "r": r, "g": g, "b": b})

#These function will control commands to relay which will connect to the fan
def fanControls(state: bool):
    payload = {
        "id": 1,
        "method": "Switch.Set",
        "params": {
            "id": 0,
            "on": state
        }
    }

    try:
        response = requests.post(url, json=payload, timeout=2)
        response.raise_for_status()
        print("Success:", response.json())
    except requests.exceptions.RequestException as e:
        print("Error:", e)


# HANDLERS THAT SEND TO BACK TO THE NODE AND AI
# ----------------------------------------------------------------------------------------------------------------------

# this function is used to send any temperture commands if any change is needed
async def handle_temp_data(src_id, data, time):
    type = data.get("type")
    temp_f = float(data.get("temp_f"))
    humidity_percent = float(data.get("humidity_percent"))

    logger.info(f"The temp is {temp_f} and the humidity is {humidity_percent}%")
    # data sent back to the node 
    send_data = {
        "target": src_id,
        "cmd": "SET_TEMP",
        "timestamp": str(datetime.now().strftime("&Y-&m-&d &H:&M:&S")),
        "humidity": humidity_percent,
        "value": temp_f
    }
    # node sent to the ai
    send_data_ai = {
        "type": type,
        "timestamp": time,
        "temperature_f": temp_f,
        "humidity_percent": humidity_percent
    }
    # this is what the server sends back to the display
    send_data_display = {
        "src_id": src_id,
        "mode": "SEND_DATA",
        "node_type": "SENSOR",
        "sensor_type": 2,
        "data": {
            "type": "temp_hum",
            "temp_f": temp_f,
            "humidity_percent": float(humidity_percent)
        },
        "timestamp": int(datetime.now().timestamp() * 1000)
    }

    try:
        logger.info(f"sent the data to the IPad to display")
        # this will send the data back to the IPad to display on the GUI
        await IPAD_ID.send(json.dumps(send_data_display))
    except ConnectionClosed:
        logger.info(f"Ipad is not connected")
    except Exception as e:
        logger.info("info could not be sent to IPAD")

    # this will store the data in json file so the AI can use for later
    # store_json_commands(send_data)
    # this sends the data to the ai model and logs what happens
    await give_data_to_ai(send_data_ai)
    await store_json_commands(data)



# this function handles data from the water sensor and sends it to the AI when the event when using water says stop
async def handle_light_data(src_id, data, time):
    global CURR_BRIGHTNESS, NO_MOTION_CNT, LIGHT_PRE_STATE
    # data that is sent to the AI
    if data.get("lux") < 0:
        lux_data = CURR_BRIGHTNESS
    else:
        lux_data = data.get("lux")
    try:
        lux_value = float(lux_data)
    except (TypeError, ValueError):
        logger.info(f"could not convert lux to float from {src_id}: {lux_data}")
        return

    isOn = data.get("isOn")
    logger.info(f"the isOn is {isOn}")
    send_data_client = {}
    if (isOn == True):
        LIGHT_PRE_STATE = "Light_On"
        logger.info("The light is on")
        turn_on_light()
        set_light_brightness(int(lux_value))
        CURR_BRIGHTNESS = int(lux_value)
        if "set_rgb" in data:
            rgb_values = data.get("set_rgb")
            red = rgb_values.get("red")
            green = rgb_values.get("green")
            blue = rgb_values.get("blue")
            set_brightness_and_color(CURR_BRIGHTNESS, red, green, blue)
        NO_MOTION_CNT = 0
        send_data_ai = {
            "type": "lux",
            "timestamp": int(datetime.now().timestamp() * 1000),
            "lux": float(lux_value),
            "command": "Light_On"
        }
        logger.info(f"Brightness is {lux_data}")
        await give_data_to_ai(send_data_ai)
    else:
        if LIGHT_PRE_STATE != "Light_Off":
            NO_MOTION_CNT = 3
            LIGHT_PRE_STATE = "Light_Off"
        logger.info("The light is off")
        turn_off_light()
        CURR_BRIGHTNESS = int(lux_value)
        send_data_ai = {
            "type": "lux",
            "timestamp": int(datetime.now().timestamp() * 1000),
            "lux": float(lux_value),
            "command": "Light_Off"
        }
        logger.info(f"Brightness is {lux_data}")
        await give_data_to_ai(send_data_ai)

    # this is sending data to the display
    send_data_display = {
        "src_id": LIGHT_NODE,
        "mode": "SEND_DATA",
        "node_type": "SENSOR",
        "sensor_type": 1,
        "data": {
            "type": "lux",
            "lux": lux_value,
            "isOn": isOn
        },
        "timestamp": int(datetime.now().timestamp() * 1000)
    }

    try:
        logger.info(f"sent the data to the IPad to display")
        # this will send the data back to the IPad to display on the GUI
        await IPAD_ID.send(json.dumps(send_data_display))
    except ConnectionClosed:
        logger.info(f"Ipad is not connected")
    except Exception as e:
        logger.info("info could not be sent to IPAD")

    # this sends the data to the ai model and logs what happen
    await store_json_commands(send_data_display.get("data"))


# HANDLERS THAT ONLY SEND BACK TO THE AI
# ----------------------------------------------------------------------------------------------------------------------

# This function will gather data from the motion sensor and send to the ai
async def handle_motion_data(src_id, data, time):
    global NO_MOTION_CNT
    type = data.get("type")
    motion_set = data.get("is_motion")

    if motion_set == "false":
        motion_value = 0
        NO_MOTION_CNT += 1
        logger.info(f"No Motion detected. no motion_count is now {NO_MOTION_CNT}")
    elif motion_set == "true":
        logger.info(f"Motion Detected")
        motion_value = 1
    logger.info(f"motion_value: {motion_value}")

    send_data_ai = {
        "type": type,
        "timestamp": str(datetime.now().strftime("&Y-&m-&d &H:&M:&S")),
        "motion": motion_value
    }

    if motion_value == 0: motion_bool = False
    else: motion_bool = True

    send_data_display = {
        "src_id": src_id,
        "mode": "SEND_DATA",
        "node_type": "SENSOR",
        "sensor_type": 4, # motion sensor
        "data": {
            "type": "motion",
            "is_motion": motion_bool
        },
        "timestamp": int(datetime.now().timestamp() * 1000)    
    }

    try:
        logger.info(f"sent the data to the IPad to display")
        # this will send the data back to the IPad to display on the GUI
        await IPAD_ID.send(json.dumps(send_data_display))
    except ConnectionClosed:
        logger.info(f"Ipad is not connected")
    except Exception as e:
        logger.info("info could not be sent to IPAD")

    # this will store the data in json file so the AI can use for later
    await give_data_to_ai(send_data_ai)
    await store_json_commands(data)
    # this sends the data to the ai model and logs what happens

# This function will handle data from water sensor and send over to AI
async def handle_water_data(src_id, data, time):

    #for water data, data is going to have multiple sub catagories
    type = data.get("type")
    fixture = data.get("fixture")
    amount = float(data.get("curr_usage"))



    logger.info(f"current usage is {amount} for the {fixture}")
    # make json file for what to send the AI
    send_data_ai = {
        "type": type,
        "fixture": fixture,
        "timestamp": int(datetime.now().timestamp() * 1000),
        "gallons": amount
    }

    send_data_display = {
        "src_id": src_id,
        "mode": "SEND_DATA",
        "node_type": "SENSOR",
        "sensor_type": 0, # water sensor
        "data": {
            "type": "water",
            "fixture": fixture,
            "curr_usage": amount
        },
        "timestamp": int(datetime.now().timestamp() * 1000)
    }


    try:
        logger.info(f"sent the data to the IPad to display")
        # this will send the data back to the IPad to display on the GUI
        await IPAD_ID.send(json.dumps(send_data_display))
    except ConnectionClosed:
        logger.info(f"Ipad is not connected")
    except Exception as e:
        logger.info("info could not be sent to IPAD")

    # this will store the data in json file so the AI can use for later
    # this sends the data to the ai model and logs what happens
    process_sensor_update(send_data_ai)
    result = get_water_totals()
    logger.info(f"Water amount is {result}")
    await store_json_commands(data)
    

# this functions needs to gather data from the sensor and arrage the JSON file recieved and place it into a JSON
# file that is sent to the AI
async def handle_electric_data(src_id, data, time):
    usage = data.get("usage")
    appliance = data.get("appliance")
    type = data.get("type")

    if appliance == "Fan":
        isFanOn = data.get("isOn")
        logger.info(f"Recieved the fan command: {isFanOn}")
        fanControls(isFanOn)
        if isFanOn:
            send_data_ai = {
                "type": type,
                "appliance": appliance,
                "timestamp": str(datetime.now().strftime("&Y-&m-&d &H:&M:&S")),
                "usage": usage,
                "command": "Fan_On"
            }
        else:
            send_data_ai = {
                "type": type,
                "appliance": appliance,
                "timestamp": str(datetime.now().strftime("&Y-&m-&d &H:&M:&S")),
                "usage": usage,
                "command": "Fan_Off"
            }
        send_data_display = {
            "src_id": src_id,
            "mode": "SEND_DATA",
            "node_type": "SENSOR",
            "sensor_type": 3,
            "data": {
                "type": "power",
                "fixture": appliance,
                "curr_usage": usage,
                "isOn": isFanOn
            },
            "timestamp": int(datetime.now().timestamp() * 1000)
        }
    else:
        send_data_display = {
            "src_id": src_id,
            "mode": "SEND_DATA",
            "node_type": "SENSOR",
            "sensor_type": 3,
            "data": {
                "type": "power",
                "fixture": appliance,
                "curr_usage": usage
            },
            "timestamp": int(datetime.now().timestamp() * 1000)
        }
        


    try:
        logger.info(f"sent the data to the IPad to display")
        # this will send the data back to the IPad to display on the GUI
        await IPAD_ID.send(json.dumps(send_data_display))
    except ConnectionClosed:
        logger.info(f"Ipad is not connected")
    except Exception as e:
        logger.info("info could not be sent to IPAD")

    # this will store the data in json file so the AI can use for later
    # this sends the data to the ai model and logs what happens
    await give_data_to_ai(send_data_ai)
    await store_json_commands(data)


# MAIN WEBSOCKET DATA MANAGEMENT
# ----------------------------------------------------------------------------------------------------------------------

# this function will interpret the JSON file and then place the data accordingly using place data function
async def handle_sensor_data(msg: dict) -> None:
    global LIGHT_NODE
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
        place_data(NODE_TYPE, DATA, TIMESTAMP) # put data in database

        await handle_water_data(SRC_ID, DATA, TIMESTAMP)

    elif NODE_TYPE == HVAC:
        logger.info(f"Temp: Recieved Data")
        place_data(HVAC, DATA, TIMESTAMP)
        await handle_temp_data(SRC_ID, DATA, TIMESTAMP)

    elif NODE_TYPE == LIGHT:
        logger.info(f"Light: Recieved Data")
        if LIGHT_NODE is None:
            logger.info(f"the LIGHT_NODE is {msg.get("src_id")}")
            LIGHT_NODE = msg.get("src_id")
        place_data(LIGHT, DATA, TIMESTAMP)
        await handle_light_data(SRC_ID, DATA, TIMESTAMP)

    elif NODE_TYPE == MOTION:
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
    # this will handle whether a command comes from the ipad display then it will process that information
    elif mtype == "COMMAND":
        SRC_ID = msg.get("src_id")
        DATA = msg.get("data")
        if SRC_ID == "Light_Node":
            logger.info("Recieved command for Light")
            await handle_light_data(SRC_ID, DATA, time=datetime.now())
        elif SRC_ID == "Fan_Node":
            logger.info("Recieved command for fan")
            await handle_electric_data(SRC_ID, DATA, time=datetime.now())


async def handler(ws: WebSocketServerProtocol) -> None:
    global ROOT_WS, ROOT_ID, IPAD_SRC, IPAD_ID
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
            if msg.get("type") == "Ipad_Display":
                if IPAD_ID is not ws:
                    logger.info("Ipad has joined")
                    IPAD_ID = ws
                    IPAD_SRC = msg.get("src_id")
                    logger.info(f"ipad is connected to the server: {IPAD_SRC}")
                continue
            elif node_id is None and msg.get("src_id") and msg.get("isRoot") is True and IPAD_ID is not None:
                logger.info("root is connecting")
                node_id = msg["src_id"]
                CLIENTS[node_id] = ws
                logger.info(f"node id is {node_id}")
                # if your nodes provide role, capture root
                if ws is not ROOT_WS:
                    ROOT_WS = ws
                    ROOT_ID = node_id
                    logger.info(f"root id is {ROOT_ID} and root ws is {ROOT_WS}")
            
            if ROOT_ID is not None:
                await handle_message(ws, msg)
    except ConnectionClosed:
        logger.info(f"connection closed for ws={ws}")
    except Exception as e:
        print(f"connection error was closed with {node_id} = {e}")
    finally:
        if node_id:
            CLIENTS.pop(node_id, None)
        if ROOT_WS is ws:
            ROOT_WS = None
            ROOT_ID = None
            node_id = None
            logger.info("root disconnected")
        if IPAD_ID is ws:
            IPAD_ID = None
            IPAD_SRC = None
            logger.info("iPad disconnected")
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

    # file handler (clears each run)
    file_handler = RotatingFileHandler(
        "server.log",
        maxBytes=5_000_000,
        backupCount=3,
        mode="w"  # ⭐ overwrite on start
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