#ifndef Outputs_h
#define Outputs_h

#include <stdint.h>
#include "wlr-output-management-unstable-v1-client-protocol.h"

typedef struct {
    int active;
    uint32_t global_name;
    struct wl_output *output;
    char con_name[256];
    struct zwlr_gamma_control_v1 *gamma_control;
    uint32_t gamma_size;

    // Color management state
    struct wp_color_management_output_v1  *cm_output;
    struct wp_image_description_v1        *image_desc;

    int is_hdr;
    int tf_named; // Supercedes luminance heuristic
} output_info;

typedef struct {
    int index;
    struct zwlr_output_head_v1 *head;
    int32_t enabled;
    char *name;
    char *make;
    char *model;
} head_state;

extern const struct zwlr_output_manager_v1_listener manager_listener;
extern output_info outputs[];
extern int output_count;

head_state* get_head_state(char *name);
void refresh_all_outputs(void);

#endif