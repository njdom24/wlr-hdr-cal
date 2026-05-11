#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "config.h"
#include "cm.h"
#include "outputs.h"
#include "color-management-v1-client-protocol.h"
#include "wlr-gamma-control-unstable-v1-client-protocol.h"

// --- HDR10 PQ Mapping ---
// Reference: https://www.itu.int/rec/R-REC-BT.2100/en
#define PQ_MAX 10000.0
#define M1 (2610.0 / 16384)
#define M2 ((2523.0 / 4096) * 128)
#define C2 ((2413.0 / 4096) * 32)
#define C3 ((2392.0 / 4096) * 32)
#define C1 (C3 - C2 + 1)

// OETF (Inverse EOTF): nits -> PQ signal [0..1]
static double nits_to_pq(double nits) {
    double normalized = nits / PQ_MAX;
    double powered = pow(normalized, M1);
    return pow((C1 + C2 * powered) / (1.0 + C3 * powered), M2);
}

// Idea: Say gamma ramp size is 4096. From an input nits value, we want to figure out where it belongs in the gamma ramp. For example, 0.5 in PQ should be at index 2048 in the ramp.
//   We can do this for each input nits value and output nits value, and then interpolate the gamma ramp values in between. This way we can create a custom PQ curve that maps input nits to output nits.
/*
    Protocol defines the gamma ramp as an FD of size (gamma_size * 3 channels)
    in order of Red, Green, Blue
*/
void apply_gamma_ramp(output_info *o) {
    size_t lut_len = -1;
    double *input_nits = NULL;
    double *output_nits = NULL;

    if(o->gamma_control == NULL) {
        fprintf(stderr, "Cannot acquire gamma control for %s.\n", o->con_name);
        return;
    }

    // Hash table might be better, but the list size should be small enough
    for(int j = 0; j < config_sz; j++) {
        if(strcmp(cfg[j].name, o->con_name) == 0) {
            lut_len = cfg[j].lut_len;
            input_nits = cfg[j].input_nits;
            output_nits = cfg[j].output_nits;
            break;
        }
    }

    if(lut_len <= 0) {
        fprintf(stderr, "Output %s not configured.\n", o->con_name);
        return;
    }

    size_t size = sizeof(uint16_t) * o->gamma_size * 3;
    int fd = memfd_create("gamma-ramp", 0);

    printf("%s: gamma_size=%u, fd size=%zu\n", o->con_name, o->gamma_size, size);
    if (ftruncate(fd, size) == -1) {
        fprintf(stderr, "failed to create gamma ramp for %s\n", o->con_name);
        return;
    }
    uint16_t *ramp = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    for(int j = 0; j < lut_len; j++) {
        double input_pq = nits_to_pq(input_nits[j]);
        double output_pq = nits_to_pq(output_nits[j]);
        double multiplier = output_pq / input_pq;
        printf("  %f nits -> %f nits:\t %.4f PQ -> %.4f PQ\tRamp Index: %u\tRamp Multiplier: %.4f\tRamp Value: %u\n",
                input_nits[j], output_nits[j], input_pq, output_pq, (uint32_t)(input_pq * o->gamma_size), multiplier, (uint16_t)(output_pq * multiplier * UINT16_MAX));
    }

    // Convert to PQ vals 0..1
    double input_pq[lut_len]; // VLA size should be small...
    double output_pq[lut_len];
    for(int j = 0; j < lut_len; j++) {
        input_pq[j] = nits_to_pq(input_nits[j]);
        output_pq[j] = nits_to_pq(output_nits[j]);
    }

    // Interpolate between two points
    for(int j = 0; j < lut_len - 1; j++) {
        double i1 = input_pq[j];
        double i2 = input_pq[j+1];
        double o1 = output_pq[j];
        double o2 = output_pq[j+1];

        for(int ramp_idx = (int)(i1 * o->gamma_size); ramp_idx < (int)(i2 * o->gamma_size); ramp_idx++) {
            double delta = (ramp_idx - (i1 * o->gamma_size));
            double segment_length = ((i2 - i1) * o->gamma_size);
            double t = delta / segment_length;
            double out_pq = o1 + t * (o2 - o1);

            if (out_pq < 0.0) out_pq = 0.0;
            if (out_pq > 1.0) out_pq = 1.0;

            uint16_t v = (uint16_t)(out_pq * UINT16_MAX);
            ramp[ramp_idx]                      = v;
            ramp[o->gamma_size + ramp_idx]      = v;
            ramp[o->gamma_size * 2 + ramp_idx]  = v;
        }
    }
    ramp[o->gamma_size - 1]         = UINT16_MAX;  // red
    ramp[o->gamma_size * 2 - 1]     = UINT16_MAX;  // green
    ramp[o->gamma_size * 3 - 1]     = UINT16_MAX;  // blue

    munmap(ramp, size);

    zwlr_gamma_control_v1_set_gamma(o->gamma_control, fd);
}

void unset_gamma_ramp(output_info *o) {
    if(o->gamma_control == NULL) {
        fprintf(stderr, "failed to acquire gamma control for %s.\n", o->con_name);
        return;
    }

    size_t size = sizeof(uint16_t) * o->gamma_size * 3;
    int fd = memfd_create("gamma-ramp", 0);
    if (ftruncate(fd, size) == -1) {
        fprintf(stderr, "failed to create gamma ramp for %s\n", o->con_name);
        return;
    }
    uint16_t *ramp = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    for(int i = 0; i < o->gamma_size; i++) {
        uint16_t v = (uint16_t)((double)i / (o->gamma_size - 1) * UINT16_MAX);
        ramp[i]                     = v;  // red
        ramp[o->gamma_size + i]     = v;  // green
        ramp[o->gamma_size * 2 + i] = v;  // blue
    }

    munmap(ramp, size);
    zwlr_gamma_control_v1_set_gamma(o->gamma_control, fd);
}

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

    if(o->is_hdr) {
        apply_gamma_ramp(o);
    } else {
        unset_gamma_ramp(o);
    }
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