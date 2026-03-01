#include "lightSense.h"
#include <string.h>

#define LDR_CHANNEL ADC_CHANNEL_5 // GPIO33

#define ADC_MAX 4095.0
#define VREF    3.3

lightSense lightSense_construct(void)
{
    lightSense ls;
    memset(&ls, 0, sizeof(lightSense));
    ls.sensedLux = 0.0f;
    return ls;
}

void lightSense_init(lightSense *ls)
{
    memset(ls, 0, sizeof(lightSense));

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_config, &ls->adc1_handle);
    if (err != ESP_OK) {
        ESP_LOGE("LIGHT", "ADC unit init failed: %s", esp_err_to_name(err));
        return;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(ls->adc1_handle, LDR_CHANNEL, &config);
    if (err != ESP_OK) {
        ESP_LOGE("LIGHT", "ADC channel config failed: %s", esp_err_to_name(err));
        return;
    }

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t cali_ret = adc_cali_create_scheme_line_fitting(&cali_config, &ls->cali_handle);
    if (cali_ret != ESP_OK) {
        ESP_LOGW("LIGHT", "Calibration failed: %s — falling back to raw conversion", esp_err_to_name(cali_ret));
        ls->cali_handle = NULL;
    }
}

void determineLuxLevel(lightSense *ls)
{
    int raw = 0;
    int sum = 0;
    int samples = 8;

    // small settling delay
    vTaskDelay(pdMS_TO_TICKS(10));

    for (int i = 0; i < samples; i++) {
        esp_err_t err = adc_oneshot_read(ls->adc1_handle, LDR_CHANNEL, &raw);
        if (err != ESP_OK) {
            ESP_LOGE("LIGHT", "ADC read failed: %s", esp_err_to_name(err));
            return;
        }
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    raw = sum / samples;

    if (ls->cali_handle) {
        int voltage_mv = 0;
        esp_err_t cali_err = adc_cali_raw_to_voltage(ls->cali_handle, raw, &voltage_mv);
        if (cali_err != ESP_OK) {
            ESP_LOGW("LIGHT", "Calibration convert failed: %s", esp_err_to_name(cali_err));
            ls->sensedLux = (raw / ADC_MAX) * VREF;
        } else {
            ls->sensedLux = voltage_mv / 1000.0f;
        }
    } else {
        ls->sensedLux = (raw / ADC_MAX) * VREF;
    }

    ls->isOn = (ls->sensedLux > 1.0f);

    ESP_LOGI("LIGHT", "Raw: %d | Voltage: %.3f V | isOn: %s",
             raw, ls->sensedLux, ls->isOn ? "true" : "false");
}