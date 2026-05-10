#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cm.h"
#include "color-management-v1-client-protocol.h"

// Image listeners
// Used to determine active TF

static void info_tf_named(void *data, struct wp_image_description_info_v1 *info, uint32_t tf) {
    output_info *o = data;

    o->is_hdr = (tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
    o->tf_named = 1;
    fprintf(stderr, "tf_named: %u\n", tf);
}

static void info_tf_power(void *data, struct wp_image_description_info_v1 *info, uint32_t eexp) { }

static void info_done(void *data, struct wp_image_description_info_v1 *info) {
    output_info *o = data;
    fprintf(stderr, "info done, %s is_hdr=%d\n", o->con_name, o->is_hdr);
}

static void info_icc_file(void *data, struct wp_image_description_info_v1 *info, int32_t icc, uint32_t icc_size) { }
static void info_primaries(void *data, struct wp_image_description_info_v1 *info, int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y, int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y) { }
static void info_primaries_named(void *data, struct wp_image_description_info_v1 *info, uint32_t primaries) { }

static void info_luminances(void *data, struct wp_image_description_info_v1 *info, uint32_t min_lum, uint32_t max_lum, uint32_t reference_lum) {
    fprintf(stderr, "reference_lum=%d max_lum=%d\n", reference_lum, max_lum);

    output_info *o = data;

    if(!o->tf_named) {
        o->is_hdr = (reference_lum >= 203 || max_lum >= 10000);
    }
}

static void info_target_primaries(void *data, struct wp_image_description_info_v1 *info, int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y, int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y) { }
static void info_target_luminance(void *data, struct wp_image_description_info_v1 *info, uint32_t min_lum, uint32_t max_lum) { }
static void info_target_max_cll(void *data, struct wp_image_description_info_v1 *info, uint32_t max_cll) { }
static void info_target_max_fall(void *data, struct wp_image_description_info_v1 *info, uint32_t max_fall) { }

static const struct wp_image_description_info_v1_listener info_listener = {
    .icc_file          = info_icc_file,
    .primaries         = info_primaries,
    .primaries_named   = info_primaries_named,
    .tf_power          = info_tf_power,
    .tf_named          = info_tf_named,
    .luminances        = info_luminances,
    .target_primaries  = info_target_primaries,
    .target_luminance  = info_target_luminance,
    .target_max_cll    = info_target_max_cll,
    .target_max_fall   = info_target_max_fall,
    .done              = info_done,
};

// Output-specific listeners
// Used to set up image description listeners

static void image_desc_failed(void *data, struct wp_image_description_v1 *desc, uint32_t cause, const char *msg) {
    fprintf(stderr, "image description failed: cause=%u msg=%s\n", cause, msg);
}

static void image_desc_ready(void *data, struct wp_image_description_v1 *desc, uint32_t identity) {
    fprintf(stderr, "image description ready\n");
    output_info *o = data;
    struct wp_image_description_info_v1 *info = wp_image_description_v1_get_information(desc);
    wp_image_description_info_v1_add_listener(info, &info_listener, o);
}

static void image_desc_ready2(void *data, struct wp_image_description_v1 *desc, uint32_t identity_hi, uint32_t identity_lo) {
    fprintf(stderr, "image description ready2\n");
    output_info *o = data;
    struct wp_image_description_info_v1 *info = wp_image_description_v1_get_information(desc);
    wp_image_description_info_v1_add_listener(info, &info_listener, o);
}

static const struct wp_image_description_v1_listener image_desc_listener = {
    .failed = image_desc_failed,
    .ready  = image_desc_ready,
    .ready2 = image_desc_ready2,
};

// Set up "getter" for current TF
static void fetch_image_description(output_info *o) {
    if (o->image_desc) {
        wp_image_description_v1_destroy(o->image_desc);
        o->image_desc = NULL;
    }
    o->tf_named = 0;
    o->is_hdr = 0;
    o->image_desc = wp_color_management_output_v1_get_image_description(o->cm_output);
    wp_image_description_v1_add_listener(o->image_desc, &image_desc_listener, o);
}

// Refresh TF metadata on change
static void cm_output_image_description_changed(void *data, struct wp_color_management_output_v1 *cm_output) {
    fprintf(stderr, "output image description changed (HDR/SDR switch?)\n");
    output_info *o = data;
    fetch_image_description(o);
}

static const struct wp_color_management_output_v1_listener cm_output_listener = {
    .image_description_changed = cm_output_image_description_changed,
};

// Global CM listeners

static void cm_supported_intent(void *data, struct wp_color_manager_v1 *cm, uint32_t render_intent) { }
static void cm_supported_feature(void *data, struct wp_color_manager_v1 *cm, uint32_t feature) { }
static void cm_supported_tf_named(void *data, struct wp_color_manager_v1 *cm, uint32_t tf) { }
static void cm_supported_primaries_named(void *data, struct wp_color_manager_v1 *cm, uint32_t primaries) { }

static void cm_manager_done(void *data, struct wp_color_manager_v1 *cm) {
    fprintf(stderr, "cm_manager_done\n");
}

const struct wp_color_manager_v1_listener color_manager_listener = {
    .supported_intent          = cm_supported_intent,
    .supported_feature         = cm_supported_feature,
    .supported_tf_named        = cm_supported_tf_named,
    .supported_primaries_named = cm_supported_primaries_named,
    .done                      = cm_manager_done,
};

void cm_init_output(struct wp_color_manager_v1 *color_manager, output_info *o) {
    if (!o->output || o->cm_output) {
        fprintf(stderr, "cannot create listener for output %s.\n", o->con_name);

        if(!o->output) {
            fprintf(stderr, "no wl_output for %s\n", o->con_name);
        }
        if(o->cm_output) {
            fprintf(stderr, "already cm_output for %s\n", o->con_name);
        }
        return;
    }
    o->cm_output = wp_color_manager_v1_get_output(color_manager, o->output);
    wp_color_management_output_v1_add_listener(o->cm_output, &cm_output_listener, o);
    fetch_image_description(o);
    fprintf(stderr, "created listener for output %s\n", o->con_name);
}