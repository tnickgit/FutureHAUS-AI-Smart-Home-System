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
