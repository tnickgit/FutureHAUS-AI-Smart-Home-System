#ifndef MOTIONSENSE_H_
#define MOTIONSENSE_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// --- Pin Definitions ---
#define PIR_PIN     GPIO_NUM_4
#define LED_PIN     GPIO_NUM_2

// How long (ms) after signal goes LOW before we declare motion "ended"
#define PAUSE_TIME_MS   5000

// Calibration time in seconds (PIR warm-up)
#define CALIBRATION_SEC 15

// Struct mirrors the Arduino sketch's globals, kept together per project style
struct _motionSense {
    float    sensedMotion;      // ms elapsed since last motion event (matches sensorNode usage)
    int64_t  lastMotion;        // esp_timer timestamp (ms) of last HIGH edge
    bool     isMotionSensed;    // true during an active motion window

    // Internal state (mirrors Arduino sketch variables)
    gpio_num_t pirPin;
    gpio_num_t ledPin;
    bool       lockLow;         // true = waiting for the next motion event
    bool       takeLowTime;     // true = need to latch the LOW start time
    int64_t    lowIn;           // esp_timer timestamp (ms) when signal went LOW
    int64_t    pauseTime;       // ms to wait before declaring motion ended
};

typedef struct _motionSense motionSense;

// Constructor — zero-initialises and sets pin/timing defaults
motionSense motionSense_construct(void);

// Call once after construct — configures GPIOs and runs PIR calibration delay
void motionSense_init(motionSense *ms);

// Call repeatedly (e.g. from sensorNode_get_data) — reads PIR pin and updates state
void determineMotion(motionSense *ms);

#endif // MOTIONSENSE_H_