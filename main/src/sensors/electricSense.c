#include "electricSense.h"

electricSense elecSense_construct(void)
{
    electricSense es;
    memset(&es, 0, sizeof(electricSense));  // zero everything first
    es.appliance = "";
    return es;
}

void determineElecUsage(electricSense *es)
{
    Appliance app = rand() % 3; // later in the code add it so if recieved a certain command from JSON set it to the corresponding code
    switch (app)
    {
    case FAN:
        int fanUsage = rand() % 220;
        if (fanUsage >= 120)
        {
            es->usage = 120;
            es->totalUsage += es->usage;
            es->appliance = "Fan on";
        }
        else
        {
            es->usage = 0;
            es->appliance = "Fan off";
        }
        break;
    case LIGHTS:
        int lightUsage = rand() % 25;
        if (lightUsage >= 12)
        {
            es->usage = lightUsage;
            es->totalUsage += es->usage;
            es->appliance = "Lights on";
        }
        else
        {
            es->usage = 0;
            es->appliance = "Lights off";
        }
        break;
    case LAUNDRY_MACHINE:
        int machineUsage = rand() % 360;
        if (machineUsage >= 120 && machineUsage < 240)
        {
            es->usage = 120;
            es->totalUsage += es->usage;
            es->appliance = "Washer on";
        }
        else if (machineUsage >= 240){
            es->usage = 240;
            es ->totalUsage += es->usage;
            es->appliance = "Dryer on";
        }
        else
        {
            es->usage = 0;
            es->appliance = "Washer and Dryer is off";
        }
        break;
    default:
        printf("unknown fixture");
        break;
    }
}

void print_Elec_Data(const electricSense *es) {
    char str[500];

    sprintf(str, "Appliance: %s, usage: %d v, total: %d v\n",
            es->appliance, es->usage, es->totalUsage);

    printf(str);
}