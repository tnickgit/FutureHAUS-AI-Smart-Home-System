import os
import json
import logging
import warnings
from typing import Any, Dict, Optional

import torch
from transformers import AutoTokenizer, AutoModelForCausalLM

os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
warnings.filterwarnings("ignore")

logging.getLogger("transformers").setLevel(logging.CRITICAL)
logging.getLogger("torch").setLevel(logging.ERROR)
logging.getLogger("absl").setLevel(logging.ERROR)

NO_MOTION_COUNT: int = 0
NO_MOTION_THRESHOLD: int = 3

LATEST: Dict[str, Any] = {
    "motion": 0,
    "lux": 0.0,
    "temperature_f": 0.0,
    "humidity_percent": 0.0,
    "light_button_command": None,
    "fan_button_command": None,
}

WATER_BY_FIXTURE: Dict[str, float] = {
    "sink": 0.0,
    "toilet": 0.0,
    "shower": 0.0,
    "washer": 0.0,
}

MODEL_NAME = "microsoft/Phi-3-mini-4k-instruct"

_tokenizer = None
_model = None

def load_phi3_model():
    global _tokenizer, _model

    if _tokenizer is None or _model is None:
        _tokenizer = AutoTokenizer.from_pretrained(
            MODEL_NAME,
            trust_remote_code=True
        )

        _model = AutoModelForCausalLM.from_pretrained(
            MODEL_NAME,
            torch_dtype=torch.float16 if torch.cuda.is_available() else torch.float32,
            device_map="auto",
            trust_remote_code=True
        )

    return _tokenizer, _model

def generate_phi3_response(prompt: str, max_new_tokens: int = 120) -> str:
    tokenizer, model = load_phi3_model()

    messages = [
        {
            "role": "system",
            "content": (
                "You are a smart home assistant. "
                "Give short, friendly, practical advice. "
                "Do not mention that you are an AI model."
            )
        },
        {
            "role": "user",
            "content": prompt
        }
    ]

    text = tokenizer.apply_chat_template(
        messages,
        tokenize=False,
        add_generation_prompt=True
    )

    inputs = tokenizer(text, return_tensors="pt").to(model.device)

    with torch.no_grad():
        outputs = model.generate(
            **inputs,
            max_new_tokens=max_new_tokens,
            temperature=0.6,
            do_sample=True,
            pad_token_id=tokenizer.eos_token_id
        )

    generated = outputs[0][inputs["input_ids"].shape[-1]:]
    response = tokenizer.decode(generated, skip_special_tokens=True)

    return response.strip()

def decide_light(
    motion: Optional[int],
    lux: Optional[float],
    command: Optional[str] = None
) -> str:
    cmd_light = str(command) if command is not None else None

    if cmd_light == "Light_On":
        return "on"

    if cmd_light == "Light_Off":
        return "off"

    if lux is not None and float(lux) >= 70.0:
        return "off"

    return "on" if motion == 1 else "off"

def decide_fan(
    temp_f: Optional[float],
    humidity: Optional[float],
    command: Optional[str] = None
) -> str:
    cmd_fan = str(command) if command is not None else None

    if cmd_fan == "Fan_On":
        return "on"

    if cmd_fan == "Fan_Off":
        return "off"

    hum_high = humidity is not None and float(humidity) >= 60.0
    temp_high = temp_f is not None and float(temp_f) >= 75.0

    return "on" if (hum_high or temp_high) else "off"


def run_rule_based_control() -> Dict[str, str]:
    cmd_light = LATEST.get("light_button_command")
    cmd_fan = LATEST.get("fan_button_command")

    light = decide_light(
        LATEST.get("motion"),
        LATEST.get("lux"),
        cmd_light
    )

    fan = decide_fan(
        LATEST.get("temperature_f"),
        LATEST.get("humidity_percent"),
        cmd_fan
    )

    if cmd_light in ("Light_On", "Light_Off"):
        LATEST["light_button_command"] = None

    if cmd_fan in ("Fan_On", "Fan_Off"):
        LATEST["fan_button_command"] = None

    return {
        "light": light,
        "fan": fan,
    }

def classify_water(total: float) -> str:
    if total < 80.0:
        return "good"
    if total <= 100.0:
        return "normal"
    return "inefficient"


def top_water_fixture(day: Dict[str, float]) -> Optional[str]:
    fixtures = ["sink", "toilet", "shower", "washer"]

    if not day:
        return None

    return max(fixtures, key=lambda f: float(day.get(f, 0.0)))


def water_recommendation_phi3(day: Dict[str, float]) -> Dict[str, str]:
    total = round(float(day.get("total", 0.0)), 1)
    status = classify_water(total)
    top = top_water_fixture(day)

    prompt = f"""
Daily water usage data:
- Sink: {round(float(day.get("sink", 0.0)), 1)} gallons
- Toilet: {round(float(day.get("toilet", 0.0)), 1)} gallons
- Shower: {round(float(day.get("shower", 0.0)), 1)} gallons
- Washer: {round(float(day.get("washer", 0.0)), 1)} gallons
- Total: {total} gallons
- Usage status: {status}
- Highest usage fixture: {top}

Write one short water-saving recommendation for the user.

Rules:
- If status is good, congratulate the user for efficient water use.
- If status is normal, say they are doing well and suggest one small improvement.
- If status is inefficient, encourage them and give one specific suggestion based on the highest usage fixture.
- Keep it under 2 sentences.
"""

    try:
        recommendation = generate_phi3_response(prompt)
    except Exception as e:
        recommendation = (
            f"Water usage today was {total} gallons. "
            f"Please review your {top} usage and consider reducing it where possible."
        )

    return {
        "water_recommendation": recommendation,
        "reason": f"Total water today was {total} gallons, which is classified as {status}.",
    }

def water_recommendation_rule(day: Dict[str, float]) -> Dict[str, str]:
    return water_recommendation_phi3(day)


def process_sensor_update(record: Dict[str, Any]) -> Dict[str, Any]:
    global NO_MOTION_COUNT

    rtype = str(record.get("type", "")).strip()

    if rtype == "motion":
        m = int(record.get("motion", 0))

        if m == 1:
            LATEST["motion"] = 1
            NO_MOTION_COUNT = 0
        else:
            NO_MOTION_COUNT += 1
            if NO_MOTION_COUNT >= NO_MOTION_THRESHOLD:
                LATEST["motion"] = 0

    elif rtype == "lux":
        LATEST["lux"] = float(record.get("lux", 0.0))

        if "command" in record and record.get("command") is not None:
            LATEST["light_button_command"] = str(record.get("command"))

    elif rtype == "temp_hum":
        if "temperature_f" in record:
            LATEST["temperature_f"] = float(record.get("temperature_f", 0.0))
        elif "temp_f" in record:
            LATEST["temperature_f"] = float(record.get("temp_f", 0.0))

        LATEST["humidity_percent"] = float(record.get("humidity_percent", 0.0))

        if "command" in record and record.get("command") is not None:
            fan_cmd = str(record.get("command"))
            if fan_cmd in ("Fan_On", "Fan_Off"):
                LATEST["fan_button_command"] = fan_cmd

    elif rtype == "fan":
        if "command" in record and record.get("command") is not None:
            LATEST["fan_button_command"] = str(record.get("command"))

    elif rtype == "power":
        if "appliance" in record and str(record.get("appliance")).lower() == "fan":
            if "command" in record and record.get("command") is not None:
                LATEST["fan_button_command"] = str(record.get("command"))

    elif rtype == "water":
        fixture = str(record.get("fixture", "sink")).lower()
        gallons = float(record.get("gallons", 0.0))

        f = fixture if fixture in WATER_BY_FIXTURE else "sink"
        WATER_BY_FIXTURE[f] += gallons

    decisions = run_rule_based_control()

    return {
        "light": decisions["light"],
        "fan": decisions["fan"],
    }

def get_water_totals() -> Dict[str, float]:
    totals = {k: float(v) for k, v in WATER_BY_FIXTURE.items()}
    totals["total"] = float(sum(totals.values()))
    return totals


def daily_water_advice() -> Dict[str, Any]:
    day_data = get_water_totals()
    rec = water_recommendation_rule(day_data)

    return {
        "daily_water_totals": {k: round(v, 1) for k, v in day_data.items()},
        "daily_water_advice": rec,
    }