#ifndef Outputs_h
#define Outputs_h

#include <stdint.h>
#include "wlr-output-management-unstable-v1-client-protocol.h"

typedef struct {
    int index;
    struct zwlr_output_head_v1 *head;
    int32_t enabled;
    char *name;
    char *make;
    char *model;
} head_state;

extern const struct zwlr_output_manager_v1_listener manager_listener;

head_state* get_head_state(char *name);

#endif