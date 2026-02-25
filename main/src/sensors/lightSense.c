#include "lightSense.h"

lightSense lightSense_construct(void)
{
    lightSense ls;          // local variable
    ls.sensedLux = 0.0f;

    ls.isOn = 0;
    return ls;                    // returned by value to caller
}

void determineLuxLevel(lightSense *ls)
{
    // temperature in fahrenheit with 1 decimal place
    ls->sensedLux = 23.0f + ((float)(rand() % 620)) / 10.0f;
    if(ls->sensedLux > 50){
        ls->isOn = true;
    }
    else{
        ls->isOn = false;
    }
}
