#ifndef ELECTRICSENSE_H_
#define ELECTRICSENSE_H_

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define limit 500
typedef enum {
    FAN,
    LIGHTS,
    LAUNDRY_MACHINE
} Appliance;

// Define the struct with leading underscore
struct _electricsense {
    int usage;          // random number generated
    int totalUsage;           // total amount used
    char* appliance;
    char determination[limit];   // message string
};

typedef struct _electricsense electricSense;

electricSense elecSense_construct(void);

void determineElecUsage(electricSense *es); // this is where a number is going to be randomly generated

void print_Elec_Data(const electricSense *es);

#endif