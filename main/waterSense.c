#include "waterSense.h"

waterSense waterSense_construct(void)
{
    waterSense ws;
    ws.sensedNumber = 0;                // default starting value
    ws.totalNumber = 0;
    strcpy(ws.determination, "Unknown"); // default string
    return ws;
}

void determineWaterLevel(waterSense *ws)
{
    static int totalNumber = 0;
    ws->totalNumber = totalNumber; // previous totalNumber
    if ((rand() % 100) < 25){
        ws->sensedNumber = rand() % 20; 
        ws->totalNumber += ws->sensedNumber;
        totalNumber = ws->totalNumber; // keep the number
    }
    else{
        ws->sensedNumber = 0;
    }
}

void determineWaterData(waterSense *ws)
{
    if (ws->totalNumber < 80)
    {
        strcpy(ws->determination, "Your water level usage is perfect. ");
    }
    else if ((ws->totalNumber >= 80) && (ws->totalNumber <= 100))
    {
        strcpy(ws->determination, "Your water level usage is normal. ");
    }
    else if (ws->totalNumber > 100)
    {
        strcpy(ws->determination, "Your water level usage is a bit too high! ");
    }
}

void combineData(waterSense *ws){
    char str[64];  // bigger buffer just in case
    sprintf(str, "Usage: %d gal/day, Total: %d gal/day", ws->sensedNumber, ws->totalNumber);

    // this line just puts the entire message into determination array
    strncat(ws->determination, str, sizeof(ws->determination) - strlen(ws->determination) - 1);
}