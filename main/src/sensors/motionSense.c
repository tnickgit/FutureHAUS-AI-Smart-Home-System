#include "motionSense.h"
#include "esp_timer.h"

motionSense motionSense_construct(void)
{
    motionSense ms;          // local variable
    ms.sensedMotion = 0.0f;
    ms.lastMotion = 0;
    return ms;                    // returned by value to caller
}

void determineMotion(motionSense *ms)
{
    // temperature in fahrenheit with 1 decimal place
    ms->lastMotion = esp_timer_get_time() / 1000;
    ms->sensedMotion = esp_timer_get_time() / 1000 - ms->lastMotion;
}
