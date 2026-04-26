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

struct output_info {
    struct wl_output *output;
    int32_t x, y, width, height;
    int32_t refresh;
    char make[256], model[256], con_name[256];
    struct zwlr_gamma_control_v1 *gamma_control;
    uint32_t gamma_size;
};

static struct output_info outputs[16];
static int output_count = 0;
static struct zwlr_gamma_control_manager_v1 *gamma_manager = NULL;

// --- Gamma Control ---

static void gamma_control_gamma_size(void *data, struct zwlr_gamma_control_v1 *control, uint32_t size) {
    struct output_info *info = data;
    info->gamma_size = size;
    printf("  %s: gamma ramp size = %u\n", info->con_name, size);
}

static void gamma_control_failed(void *data, struct zwlr_gamma_control_v1 *control) {
    struct output_info *info = data;
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
    struct output_info *info = data;
    strncpy(info->con_name, name, 255);
}

static void output_description(void *data, struct wl_output *wl_output,
    const char *description) {}

static void output_geometry(void *data, struct wl_output *wl_output,
    int32_t x, int32_t y, int32_t w, int32_t h,
    int32_t subpixel, const char *make, const char *model, int32_t transform)
{
    struct output_info *info = data;
    info->x = x; info->y = y;
    strncpy(info->make, make, 255);
    strncpy(info->model, model, 255);
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        struct output_info *info = data;
        info->width = width;
        info->height = height;
        info->refresh = refresh;
    }
}

static void output_done(void *data, struct wl_output *wl_output) {}
static void output_scale(void *data, struct wl_output *wl_output, int32_t factor) {}

static const struct wl_output_listener output_listener = {
    output_geometry, output_mode, output_done, output_scale, output_name, output_description
};

// Hook up to display events
static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_output_interface.name) == 0) {
        struct output_info *info = &outputs[output_count++];
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

    wl_display_dispatch(display);   // collect globals
    wl_display_roundtrip(display);  // collect output events (may call global_remove when the server destroys globals)

    printf("Found %d display(s):\n", output_count);
    for (int i = 0; i < output_count; i++) {
        struct output_info *o = &outputs[i];
        printf("  Display %d (%s): %s %s @ %dx%d+%d+%d, %.2f Hz\n",
               i, o->con_name, o->make, o->model,
               o->width, o->height, o->x, o->y,
               o->refresh / 1000.0);
    }
    printf("------------------------------------------------------------------------------\n");

    if (!gamma_manager) {
        fprintf(stderr, "Compositor does not support zwlr_gamma_control_manager_v1\n");
        wl_display_disconnect(display);
        return 1;
    }

    // Get gamma control for each output and listen for gamma_size
    for (int i = 0; i < output_count; i++) {
        struct output_info *o = &outputs[i];
        o->gamma_control = zwlr_gamma_control_manager_v1_get_gamma_control(gamma_manager, o->output);
        zwlr_gamma_control_v1_add_listener(o->gamma_control, &gamma_control_listener, o);
    }

    wl_display_roundtrip(display); // Collect gamma_size events

    /*
      Protocol defines the gamma ramp as an FD of size (gamma_size * 3 channels)
      in order of Red, Green, Blue
    */
    for (int i = 0; i < output_count; i++) {
        struct output_info *o = &outputs[i];
        size_t size = sizeof(uint16_t) * o->gamma_size * 3;
        int fd = memfd_create("gamma-ramp", 0);

        printf("gamma_size=%u, fd size=%zu\n", o->gamma_size, size);
        ftruncate(fd, size);
        uint16_t *ramp = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

        // Interpolate from 0..1 (0..UINT16_MAX)
        for (uint32_t j = 0; j < o->gamma_size; j++) {
            uint16_t v = (uint16_t)(j * UINT16_MAX / (o->gamma_size - 1));
            ramp[j]                  = v;  // red
            ramp[o->gamma_size + j]  = v;  // green
            ramp[o->gamma_size * 2 + j] = v;  // blue
        }
        ramp[o->gamma_size * 3 - 1] = UINT16_MAX;

        munmap(ramp, size);

        zwlr_gamma_control_v1_set_gamma(o->gamma_control, fd);
    }

    while (wl_display_dispatch(display) != -1) {
        // keeps processing events until the connection drops or Ctrl+C
    }

    wl_display_disconnect(display);
    return 0;
}