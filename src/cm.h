#ifndef Cm_h
#define Cm_h

#include <stdint.h>
#include "color-management-v1-client-protocol.h"

typedef struct {
    struct wl_output     *output;
    const char           *output_name;

    struct wp_color_management_output_v1  *cm_output;
    struct wp_image_description_v1        *image_desc;

    int                   is_hdr;
    int                   tf_named; // Supercedes luminance heuristic
} cm_state;

extern const struct wp_color_manager_v1_listener color_manager_listener;
void cm_init_output(struct wp_color_manager_v1 *color_manager, cm_state *s);

#endif