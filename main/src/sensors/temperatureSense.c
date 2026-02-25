#include "temperatureSense.h"

temperatureSense temperatureSense_construct(void)
{
    temperatureSense ts;          // local variable
    ts.sensedTemperature = 0.0f;
    ts.sensedHumidity = 0.0f;
    return ts;                    // returned by value to caller
}

void determineTemperatureLevel(temperatureSense *ts)
{
    // temperature in fahrenheit with 1 decimal place
    ts->sensedTemperature = 23.0f + ((float)(rand() % 620)) / 10.0f;
    ts->sensedHumidity = 23.0f + ((float)(rand() % 620)) / 10.0f;
}
