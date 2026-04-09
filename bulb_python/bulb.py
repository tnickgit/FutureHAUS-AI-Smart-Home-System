import requests

BULB_IP = "192.168.1.85"
BASE_URL = f"http://{BULB_IP}/light/kauf_bulb"

def turn_on():
    requests.post(f"{BASE_URL}/turn_on")

def turn_off():
    requests.post(f"{BASE_URL}/turn_off")

def set_brightness(level: int):
    # level 0-255
    requests.post(f"{BASE_URL}/turn_on", params={"brightness": level})

def set_color(r: int, g: int, b: int):
    # r, g, b are 0-255
    requests.post(f"{BASE_URL}/turn_on", params={"r": r, "g": g, "b": b})

def set_brightness_and_color(level: int, r: int, g: int, b: int):
    requests.post(f"{BASE_URL}/turn_on", params={"brightness": level, "r": r, "g": g, "b": b})


# --- test it ---
if __name__ == "__main__":
    turn_on()
    set_brightness_and_color(128, 1, 1, 1)