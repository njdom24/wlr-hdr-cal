#ifndef Bluelight_h
#define Bluelight_h

#include <stdint.h>
#include <stdlib.h>

typedef struct {
    double r;
    double g;
    double b;
} bluelight_rgb;

bluelight_rgb bluelight_temp_to_rgb(uint32_t kelvin);

// Global current temperature; default 6500 K (no filter)
extern uint32_t bluelight_temperature;

#endif
