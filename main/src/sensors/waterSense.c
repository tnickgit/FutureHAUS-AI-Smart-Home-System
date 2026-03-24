#include "waterSense.h"

waterSense waterSense_construct(void)
{
    waterSense ws;
    ws.sensedNumber = 0;                // default starting value
    ws.totalNumber = 0;

    int randnum = rand()%4;

    switch(randnum){
        case 0:
            ws.fixture = "shower";
            break;
        case 1:
            ws.fixture = "toilet";
            break;
        case 2:
            ws.fixture = "sink";
            break;
        case 3:
            ws.fixture = "washer";
            break;
    }
    return ws;
}

void determineWaterLevel(waterSense *ws)
{
    static int totalNumber = 0;
    ws->totalNumber = totalNumber; // previous totalNumber
    if ((rand() % 100) < 50){
        ws->sensedNumber = rand() % 20; 
        ws->totalNumber += ws->sensedNumber;
        totalNumber = ws->totalNumber; // keep the number
    }
    else{
        ws->sensedNumber = 0;
    }
}
