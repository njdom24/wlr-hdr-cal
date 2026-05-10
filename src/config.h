#ifndef Config_h
#define Config_h

#include <stdlib.h>

typedef struct {
    char name[64];
    double multiplier; // TODO: Use on top of / instead of LUT
    size_t lut_len;
    double *input_nits;
    double *output_nits;
} output_config;

extern output_config *cfg;
extern int config_sz;

int config_read(output_config **out);
void config_free(output_config *out);

#endif