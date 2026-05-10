#include <wayland-client.h>
#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include <stdio.h>
#include <string.h>
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
    config_sz = config_read(&cfg);
    if(config_sz < 0) {
        fprintf(stderr, "Failed to read from config file.\n");
        return 1;
    }

    // Init CM for each output
    // TODO: Dynamically init when an output is enabled
    for (int i = 0; i < output_count; i++) {
        output_info *o = &outputs[i];

        cm_init_output(color_manager, o);
        wl_display_roundtrip(display);
        wl_display_roundtrip(display);
    }

    while (wl_display_dispatch(display) != -1) {
        // keeps processing events until the connection drops or Ctrl+C
    }

    config_free(cfg);
    wl_display_disconnect(display);
    return 0;
}