#include "waterSense.h"

static const char *TAG = "WATER";

// Pulse counter — written only from the ISR, read+reset in determineWaterLevel()
// volatile so the compiler never caches it in a register
static volatile uint32_t s_pulse_count = 0;

// ISR — runs in IRAM, must stay minimal (no floats, no ESP_LOG, no FreeRTOS calls)
static void IRAM_ATTR flow_isr_handler(void *arg)
{
    s_pulse_count++;
}

// --- Constructor ---
waterSense waterSense_construct(void)
{
    waterSense ws;
    memset(&ws, 0, sizeof(waterSense));
    ws.fixture    = FIXTURE_NAMES[rand() % FIXTURE_COUNT];
    ws.initialised = false;
    return ws;
}

// --- Init (replaces Arduino setup()) ---
void waterSense_init(waterSense *ws)
{
    // Install the GPIO ISR service (safe to call multiple times — ignore ESP_ERR_INVALID_STATE)
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(ret));
        return;
    }

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << FLOW_SENSOR_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   // INPUT_PULLUP, matches Arduino sketch
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,    // FALLING edge, matches Arduino sketch
    };
    ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = gpio_isr_handler_add(FLOW_SENSOR_PIN, flow_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(ret));
        return;
    }

    ws->initialised = true;
    ESP_LOGI(TAG, "Flow sensor initialised on GPIO %d", FLOW_SENSOR_PIN);
}

// --- Poll (call every ~1 second from a dedicated task or sensorNode_get_data) ---
// Mirrors the Arduino loop() 1-second window exactly
void determineWaterLevel(waterSense *ws)
{
    if (!ws->initialised) {
        ESP_LOGW(TAG, "determineWaterLevel called before waterSense_init");
        return;
    }

    // Snapshot and reset pulse count atomically (mirrors detachInterrupt/attachInterrupt)
    portDISABLE_INTERRUPTS();
    uint32_t pulses = s_pulse_count;
    s_pulse_count   = 0;
    portENABLE_INTERRUPTS();

    // Frequency (Hz) == pulseCount because we sample over exactly 1 second
    // Flow (L/min)   == Hz / FLOW_CALIBRATION  (5.0 for Adafruit sensor)
    ws->flowRate   = (float)pulses / FLOW_CALIBRATION;
    ws->totalLiters += (float)pulses * ML_PER_PULSE;

    // Keep the old field names in sync so sensorNode_package_data still works
    ws->sensedNumber = ws->flowRate;
    ws->totalNumber  = ws->totalLiters;

    ESP_LOGI(TAG, "Pulses: %lu | Flow: %.2f L/min | Total: %.3f L",
             (unsigned long)pulses, ws->flowRate, ws->totalLiters);
}