#ifndef LIGHTSENSE_H_
#define LIGHTSENSE_H_

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "esp_system.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX_LUX 1000

// Define the struct with leading underscore
struct _lightSense {
    float sensedLux;          // random temperature value
    bool isOn;
    adc_oneshot_unit_handle_t adc1_handle;
    adc_cali_handle_t cali_handle;
};

typedef struct _lightSense lightSense;

// Constructor
lightSense lightSense_construct(void);

void lightSense_init(lightSense *ls);

// Generate random temperature between 23°F and 85°F
void determineLuxLevel(lightSense *ls);

#endif // TEMPERATURESENSE_H_
