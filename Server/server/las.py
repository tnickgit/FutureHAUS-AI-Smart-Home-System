import pyaudio
import numpy as np
import time
import whisper
import string
import os
import ctypes
import gc
import asyncio
import websockets
import json
import re
from typing import Optional
from colorlist import COLOR_MAP

SERVER_URI = "ws://localhost:8765"

async def send_light_command(command: str):
    async with websockets.connect(SERVER_URI) as ws:
        msg = {
            "node_type": "COMMAND",
            "src_id": "Light_Node",
            "data": {
                "type": "lux",
                "lux": -1,
                "isOn": True if command == "Light_On" else False
            }
        }
        await ws.send(json.dumps(msg))


async def send_fan_command(command: str):
    async with websockets.connect(SERVER_URI) as ws:
        msg = {
            "node_type": "COMMAND",
            "src_id": "Fan_Node",
            "data": {
                "usage": 120,
                "appliance": "Fan",
                "type": "power",
                "isOn": True if command == "Fan_On" else False
            }
        }
        await ws.send(json.dumps(msg))

async def send_light_rgb(color_name: str):
    rgb = COLOR_MAP.get(color_name.lower())
    if rgb is None:
        return

    r, g, b = rgb

    async with websockets.connect(SERVER_URI) as ws:
        msg = {
            "node_type": "COMMAND",
            "src_id": "Light_Node",
            "data": {
                "type": "lux",
                "lux": -1,
                "isOn": True,
                "set_rgb": {
                    "red": r,
                    "green": g,
                    "blue": b
                }
            }
        }
        await ws.send(json.dumps(msg))

# Suppress ALSA error output
try:
    _asound = ctypes.cdll.LoadLibrary('libasound.so.2')
    _ERROR_HANDLER_FUNC = ctypes.CFUNCTYPE(
        None,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_char_p
    )
    _error_handler_cb = _ERROR_HANDLER_FUNC(lambda *_: None)
    _asound.snd_lib_error_set_handler(_error_handler_cb)
except OSError:
    pass


# Suppress JACK/OSS stderr output by redirecting fd 2 during PyAudio init
def _suppress_stderr():
    devnull = os.open(os.devnull, os.O_WRONLY)
    old = os.dup(2)
    os.dup2(devnull, 2)
    os.close(devnull)
    return old


def _restore_stderr(old):
    os.dup2(old, 2)
    os.close(old)


CHUNK = 1024
FORMAT = pyaudio.paInt16
CHANNELS = 1
RATE = 48000
RECORD_SECONDS = 7
REST_SECONDS = 3


def record_audio():
    _old_stderr = _suppress_stderr()
    p = pyaudio.PyAudio()
    _restore_stderr(_old_stderr)

    print("[*] Listening...")

    stream = p.open(
        format=FORMAT,
        channels=CHANNELS,
        rate=RATE,
        input=True,
        frames_per_buffer=CHUNK
    )

    frames = []

    for _ in range(0, int(RATE / CHUNK * RECORD_SECONDS)):
        data = stream.read(CHUNK, exception_on_overflow=False)
        frames.append(data)

    stream.stop_stream()
    stream.close()
    p.terminate()

    audio = np.frombuffer(b''.join(frames), dtype=np.int16).astype(np.float32) / 32768.0

    target_rate = 16000
    num_samples = int(len(audio) * target_rate / RATE)
    audio = np.interp(
        np.linspace(0, len(audio) - 1, num_samples),
        np.arange(len(audio)),
        audio
    ).astype(np.float32)

    print("Transcribing...")
    whisper_model = whisper.load_model("tiny.en")
    result = whisper_model.transcribe(audio, language="en", fp16=False)
    transcription = result["text"].strip()
    del whisper_model
    gc.collect()

    print(f"Transcription: {transcription}")
    return transcription

def normalize_words(text: str):
    return [w.strip(string.punctuation).lower() for w in text.split()]

def has_lights_on(text):
    words = normalize_words(text)
    has_light = "lights" in words or "light" in words
    has_on = "on" in words
    return has_light and has_on

def has_lights_off(text):
    words = normalize_words(text)
    has_light = "lights" in words or "light" in words
    has_off = "off" in words
    return has_light and has_off

def has_fan_on(text):
    words = normalize_words(text)
    has_fan = "fans" in words or "fan" in words
    has_on = "on" in words
    return has_fan and has_on

def has_fan_off(text):
    words = normalize_words(text)
    has_fan = "fans" in words or "fan" in words
    has_off = "off" in words
    return has_fan and has_off

def light_rgb(text):
    text = text.lower()

    for color in sorted(COLOR_MAP.keys(), key=len, reverse=True):
        if color in text:
            return color

    return None

if __name__ == "__main__":
    try:
        while True:
            transcription = record_audio()

            if has_lights_on(transcription):
                print("sending Light_On to server")
                asyncio.run(send_light_command("Light_On"))
                time.sleep(REST_SECONDS)
                continue

            if has_lights_off(transcription):
                print("sending Light_Off to server")
                asyncio.run(send_light_command("Light_Off"))
                time.sleep(REST_SECONDS)
                continue

            color = light_rgb(transcription)
            if color:
                print(f"sending RGB {color} to server")
                asyncio.run(send_light_rgb(color))
                time.sleep(REST_SECONDS)
                continue

            if has_fan_on(transcription):
                print("sending Fan_On to server")
                asyncio.run(send_fan_command("Fan_On"))
                time.sleep(REST_SECONDS)
                continue

            if has_fan_off(transcription):
                print("sending Fan_Off to server")
                asyncio.run(send_fan_command("Fan_Off"))
                time.sleep(REST_SECONDS)
                continue

            print("resting for " + str(REST_SECONDS) + " seconds")
            time.sleep(REST_SECONDS)

    except KeyboardInterrupt:
        print("\nStopping the script.")