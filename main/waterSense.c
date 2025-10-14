#include "waterSense.h"

waterSense waterSense_construct(void)
{
    waterSense ws;
    ws.sensedNumber = 0;
    return ws;
}

void determineWaterLevel(waterSense *ws)
{
    ws->sensedNumber = 100 + rand() % 400; 
}

void determineWaterData(waterSense *ws)
{
    if (ws->sensedNumber < 300)
    {
        strcpy(ws->determination, "Your water level usage is perfect. ");
    }
    else if ((ws->sensedNumber >= 300) && (ws->sensedNumber <= 350))
    {
        strcpy(ws->determination, "Your water level usage is normal. ");
    }
    else if (ws->sensedNumber > 350)
    {
        strcpy(ws->determination, "Your water level usage is a bit too high! ");
    }
}

void combineData(waterSense *ws){
    char str[64];  // bigger buffer just in case
    sprintf(str, "usage: %d gal/day", ws->sensedNumber);

    // this line just puts the entire message into determination array
    strncat(ws->determination, str, sizeof(ws->determination) - strlen(ws->determination) - 1);
}