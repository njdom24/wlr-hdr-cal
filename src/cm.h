#ifndef Cm_h
#define Cm_h

#include "color-management-v1-client-protocol.h"
#include "outputs.h"

extern const struct wp_color_manager_v1_listener color_manager_listener;
void cm_init_output(struct wp_color_manager_v1 *color_manager, output_info *o);
void apply_gamma_ramp(output_info *o);
void apply_blue_light_filter_sdr(output_info *o);

#endif