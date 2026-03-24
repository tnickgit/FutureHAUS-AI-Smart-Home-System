#include "motionSense.h"

static const char *TAG = "MOTION";

// --- Constructor ---
motionSense motionSense_construct(void)
{
    motionSense ms;
    ms.sensedMotion   = 0.0f;
    ms.lastMotion     = 0;
    ms.isMotionSensed = false;

    ms.pirPin    = PIR_PIN;
    ms.ledPin    = LED_PIN;
    ms.lockLow   = true;   // waiting for first motion event (matches Arduino sketch)
    ms.takeLowTime = false;
    ms.lowIn     = 0;
    ms.pauseTime = PAUSE_TIME_MS;

    return ms;
}

// --- Init (replaces Arduino setup()) ---
void motionSense_init(motionSense *ms)
{
    // Configure PIR input
    gpio_config_t pir_cfg = {
        .pin_bit_mask = (1ULL << ms->pirPin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLDOWN_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,  // keep pin from floating
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&pir_cfg);

    // Configure LED output
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << ms->ledPin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    gpio_set_level(ms->ledPin, 0);

    // PIR warm-up calibration (matches Arduino sketch's 30-second loop)
    ESP_LOGI(TAG, "Calibrating PIR sensor ");
    for (int i = 0; i < CALIBRATION_SEC; i++) {
        ESP_LOGI(TAG, "  calibrating... %d/%d", i + 1, CALIBRATION_SEC);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "PIR calibration done — SENSOR ACTIVE");
}

// --- Poll (replaces Arduino loop()) ---
// Call this from sensorNode_get_data on every sample tick.
void determineMotion(motionSense *ms)
{
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (gpio_get_level(ms->pirPin) == 1) {          // PIR HIGH — motion present
        gpio_set_level(ms->ledPin, 1);

        if (ms->lockLow) {
            // Rising edge: new motion event started
            ms->lockLow       = false;
            ms->lastMotion    = now_ms;
            ms->isMotionSensed = true;
            ESP_LOGI(TAG, "Motion detected at %lld s", now_ms / 1000);
        }

        ms->takeLowTime = true;                      // arm the LOW-latch on next LOW

    } else {                                         // PIR LOW — no signal
        gpio_set_level(ms->ledPin, 0);

        if (ms->takeLowTime) {
            ms->lowIn       = now_ms;                // latch when signal first went LOW
            ms->takeLowTime = false;
        }

        // Only declare motion "ended" after pauseTime ms of continuous LOW
        if (!ms->lockLow && (now_ms - ms->lowIn) > ms->pauseTime) {
            ms->lockLow        = true;
            ms->isMotionSensed = false;
            ESP_LOGI(TAG, "Motion ended at %lld s", (now_ms - ms->pauseTime) / 1000);
        }
    }

    // sensedMotion = ms since last detected motion (used by sensorNode_package_data)
    ms->sensedMotion = (float)(now_ms - ms->lastMotion);
}