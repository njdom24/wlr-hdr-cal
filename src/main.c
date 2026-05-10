#define _GNU_SOURCE

#include <wayland-client.h>
#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/memfd.h>
#include "config.h"
#include "outputs.h"
#include "cm.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"
#include "color-management-v1-client-protocol.h"

static output_info outputs[16];
static int output_count = 0;
static struct zwlr_output_manager_v1 *output_manager = NULL;
static struct zwlr_gamma_control_manager_v1 *gamma_manager = NULL;
static struct wp_color_manager_v1 *color_manager = NULL;

// Assuming display is in ST2084 PQ for now
//   TODO: Use color_management_v1 to know this

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

// --- Gamma Control ---

static void gamma_control_gamma_size(void *data, struct zwlr_gamma_control_v1 *control, uint32_t size) {
    output_info *info = data;
    info->gamma_size = size;
    printf("  %s: gamma ramp size = %u\n", info->con_name, size);
}

static void gamma_control_failed(void *data, struct zwlr_gamma_control_v1 *control) {
    output_info *info = data;
    fprintf(stderr, "  %s: gamma control failed\n", info->con_name);
    zwlr_gamma_control_v1_destroy(control);
    info->gamma_control = NULL;
}

static const struct zwlr_gamma_control_v1_listener gamma_control_listener = {
    .gamma_size = gamma_control_gamma_size,
    .failed     = gamma_control_failed,
};

// --- Output ---

static void output_name(void *data, struct wl_output *wl_output, const char *name) {
    output_info *info = data;
    strncpy(info->con_name, name, 255);
}

static void output_description(void *data, struct wl_output *wl_output, const char *description) { }
static void output_geometry(void *data, struct wl_output *wl_output,
    int32_t x, int32_t y, int32_t w, int32_t h,
    int32_t subpixel, const char *make, const char *model, int32_t transform) { }
static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) { }
static void output_done(void *data, struct wl_output *wl_output) { }
static void output_scale(void *data, struct wl_output *wl_output, int32_t factor) { }

static const struct wl_output_listener output_listener = {
    output_geometry, output_mode, output_done, output_scale, output_name, output_description
};

// Hook up to display events
static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_output_interface.name) == 0) {
        output_info *info = &outputs[output_count++];
        if (version < 2) {
            fprintf(stderr, "Requires %s protocol version 2. Provided %d.\n", interface, version);
            return;
        }
        if (version < 4) {
            fprintf(stderr, "Connector name requires %s protocol version 4. Provided %d.\n", interface, version);
            info->output = wl_registry_bind(registry, name, &wl_output_interface, 2);
        }
        else {
            info->output = wl_registry_bind(registry, name, &wl_output_interface, 4);
        }
        
        wl_output_add_listener(info->output, &output_listener, info);
    }
    else if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
        output_manager = wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, 4);
        zwlr_output_manager_v1_add_listener(output_manager, &manager_listener, NULL);
    }
    else if (strcmp(interface, wp_color_manager_v1_interface.name) == 0) {
        color_manager = wl_registry_bind(registry, name, &wp_color_manager_v1_interface, 1); // version 1 is fine since we only care about PQ EOTF or not
        wp_color_manager_v1_add_listener(color_manager, &color_manager_listener, NULL);
    }
    else if (strcmp(interface, zwlr_gamma_control_manager_v1_interface.name) == 0) {
        gamma_manager = wl_registry_bind(registry, name, &zwlr_gamma_control_manager_v1_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

// Intro to Wayland from https://drewdevault.com/blog/Introduction-to-Wayland/
int main(void) {
    // Establish a connection to the Wayland server
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Cannot connect to Wayland\n");
        return 1;
    }

    // Enumerates the globals available on the server
    struct wl_registry *registry = wl_display_get_registry(display);

    // Listen to events emitted by the registry
    struct wl_registry_listener registry_listener = {
        .global        = registry_global,
        .global_remove = registry_global_remove,
    };
    wl_registry_add_listener(registry, &registry_listener, NULL); // void *data unused

    wl_display_roundtrip(display);   // collect globals
    wl_display_roundtrip(display);  // collect output events (may call global_remove when the server destroys globals)

    printf("Found %d display(s):\n", output_count);
    for (int i = 0; i < output_count; i++) {
        output_info *o = &outputs[i];
        head_state *hs = get_head_state(o->con_name);
        printf("  Display %d (%s): %s %s\n",
               i, o->con_name, hs->make, hs->model);
    }
    printf("------------------------------------------------------------------------------\n");

    if (!output_manager) {
        fprintf(stderr, "Compositor does not support zwlr_output_manager_v1\n");
        wl_display_disconnect(display);
        return 1;
    }

    if (!color_manager) {
        fprintf(stderr, "Compositor does not support wp_color_management_v1\n");
        wl_display_disconnect(display);
        return 1;
    }

    if (!gamma_manager) {
        fprintf(stderr, "Compositor does not support zwlr_gamma_control_manager_v1\n");
        wl_display_disconnect(display);
        return 1;
    }

    // Get gamma control for each output and listen for gamma_size
    for (int i = 0; i < output_count; i++) {
        output_info *o = &outputs[i];
        o->gamma_control = zwlr_gamma_control_manager_v1_get_gamma_control(gamma_manager, o->output);
        zwlr_gamma_control_v1_add_listener(o->gamma_control, &gamma_control_listener, o);
    }

    wl_display_roundtrip(display); // Collect gamma_size events

    // Read from config
    output_config *cfg = NULL;
    int config_sz = config_read(&cfg);
    if(config_sz < 0) {
        fprintf(stderr, "Failed to read from config file.\n");
        return 1;
    }

    // Idea: Say gamma ramp size is 4096. From an input nits value, we want to figure out where it belongs in the gamma ramp. For example, 0.5 in PQ should be at index 2048 in the ramp.
    //   We can do this for each input nits value and output nits value, and then interpolate the gamma ramp values in between. This way we can create a custom PQ curve that maps input nits to output nits.
    /*
      Protocol defines the gamma ramp as an FD of size (gamma_size * 3 channels)
      in order of Red, Green, Blue
    */
    for (int i = 0; i < output_count; i++) {
        size_t lut_len = -1;
        double *input_nits = NULL;
        double *output_nits = NULL;

        output_info *o = &outputs[i];
        if(o->gamma_control == NULL) {
            fprintf(stderr, "Cannot acquire gamma control for %s.\n", o->con_name);
            continue;
        }

        fprintf(stderr, "output=%p\n", (void*)o);
        cm_init_output(color_manager, o);
        fprintf(stderr, "%s cm_output=%p\n", o->con_name, (void*)o->cm_output);
        wl_display_roundtrip(display);
        wl_display_roundtrip(display);

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
            continue;
        }

        size_t size = sizeof(uint16_t) * o->gamma_size * 3;
        int fd = memfd_create("gamma-ramp", 0);

        printf("%s: gamma_size=%u, fd size=%zu\n", o->con_name, o->gamma_size, size);
        if (ftruncate(fd, size) == -1) {
            fprintf(stderr, "failed to create gamma ramp for %s\n", o->con_name);
            continue;
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
        double input_pq[lut_len]; // VLA size should be small
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

    config_free(cfg);

    while (wl_display_dispatch(display) != -1) {
        // keeps processing events until the connection drops or Ctrl+C
    }

    wl_display_disconnect(display);
    return 0;
}