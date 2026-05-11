#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <tomlc17.h>

#include <string.h>

output_config *cfg = NULL;
int config_sz = -1;

double toml_to_double(toml_datum_t d) {
    if (d.type == TOML_FP64)  return d.u.fp64;
    if (d.type == TOML_INT64) return (double)d.u.int64;
    return 0.0;
}

int config_read(output_config **out) {
    char config_path[4096];
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "HOME environment variable not set\n");
        return -1;
    }
    snprintf(config_path, sizeof(config_path), "%s/.config/wlr-hdr-cal/config", home);

    toml_result_t result = toml_parse_file_ex(config_path);
    if (!result.ok) {
        fprintf(stderr, "config_read: Parse error: %s\n", result.errmsg);
        return -1;
    }

    // "monitors" is an array of tables
    toml_datum_t monitors = toml_get(result.toptab, "monitors");
    if (monitors.type != TOML_ARRAY) {
        fprintf(stderr, "config_read: 'monitors' must be an array\n");
        return -1;
    }

    printf("Found %d monitor(s)\n\n", monitors.u.arr.size);
    output_config *outputs = malloc(sizeof(*outputs) * monitors.u.arr.size);
    *out = outputs;

    for (int i = 0; i < monitors.u.arr.size; i++) {
        toml_datum_t mon = monitors.u.arr.elem[i];
        if (mon.type != TOML_TABLE) {
            fprintf(stderr, "config_read: monitors[%d] is not a table\n", i);
            continue;
        }

        printf("monitors[%d]:\n", i);

        // name
        toml_datum_t name = toml_get(mon, "name");
        if (name.type == TOML_STRING)
            printf("  name       = \"%s\"\n", name.u.s);

        // multiplier (optional, default 1.0)
        toml_datum_t mult = toml_get(mon, "multiplier");
        if (mult.type == TOML_FP64 || mult.type == TOML_INT64) {
            outputs[i].multiplier = toml_to_double(mult);
            if (outputs[i].multiplier <= 0.0) {
                fprintf(stderr, "config_read: multiplier must be > 0. Found %g\n", outputs[i].multiplier);
                toml_free(result);
                return -1;
            }
            printf("  multiplier = %g\n", outputs[i].multiplier);
        } else {
            outputs[i].multiplier = 1.0;
            printf("  multiplier = 1.0 (default)\n");
        }

        strcpy(outputs[i].name, name.u.s);

        // values = array of [lo, hi] pairs
        toml_datum_t values = toml_get(mon, "values");
        if (values.type == TOML_ARRAY) {
            printf("  values     = [\n");
            int valid_count = 0;
            int first_valid = -1;
            int last_valid = -1;
            for (int j = 0; j < values.u.arr.size; j++) {
                toml_datum_t pair = values.u.arr.elem[j];
                if (pair.type == TOML_ARRAY && pair.u.arr.size == 2) {
                    if (first_valid == -1) first_valid = j;
                    last_valid = j;
                    valid_count++;
                }
            }
            
            int clamp_start = (first_valid == -1 || toml_to_double(values.u.arr.elem[first_valid].u.arr.elem[0]) > 0.0) ? 1 : 0;
            int clamp_end = (last_valid == -1 || toml_to_double(values.u.arr.elem[last_valid].u.arr.elem[0]) < 10000.0) ? 1 : 0;

            size_t total_size = valid_count + clamp_start + clamp_end;
            size_t sz = total_size * sizeof(double);
            outputs[i].input_nits = (double*) malloc(sz);
            outputs[i].output_nits = (double*) malloc(sz);
            
            int curr = 0;
            
            if (clamp_start) {
                outputs[i].input_nits[curr] = 0.0;
                outputs[i].output_nits[curr] = 0.0;
                printf("    [0, 0] (clamped),\n");
                curr++;
            }

            for (int j = 0; j < values.u.arr.size; j++) {
                toml_datum_t pair = values.u.arr.elem[j];
                if (pair.type == TOML_ARRAY && pair.u.arr.size == 2) {
                    double in  = toml_to_double(pair.u.arr.elem[0]);
                    double out = toml_to_double(pair.u.arr.elem[1]) * outputs[i].multiplier;
                    if (out > 10000.0) out = 10000.0;
                    if (curr > 0 && in <= outputs[i].input_nits[curr - 1]) {
                        fprintf(stderr, "config_read: input nits must be strictly increasing. Found %g after %g\n", in, outputs[i].input_nits[curr - 1]);
                        toml_free(result);
                        return -1;
                    }
                    outputs[i].input_nits[curr] = in;
                    outputs[i].output_nits[curr] = out;
                    printf("    [%g, %g],\n", in, out);
                    curr++;
                }
            }
            
            if (clamp_end) {
                outputs[i].input_nits[curr] = 10000.0;
                double out = 10000.0 * outputs[i].multiplier;
                if (out > 10000.0) out = 10000.0;
                outputs[i].output_nits[curr] = out;
                printf("    [10000, %g] (clamped)\n", out);
                curr++;
            }

            outputs[i].lut_len = curr;
            printf("  ]\n");
        } else if (mult.type == TOML_FP64 || mult.type == TOML_INT64) {
            printf("  values     = (generated from multiplier %g)\n", outputs[i].multiplier);
            int steps = 100;
            size_t sz = (steps + 1) * sizeof(double);
            outputs[i].input_nits = (double*) malloc(sz);
            outputs[i].output_nits = (double*) malloc(sz);
            
            for (int j = 0; j <= steps; j++) {
                double in = (10000.0 / steps) * j;
                double out = in * outputs[i].multiplier;
                if (out > 10000.0) out = 10000.0;
                outputs[i].input_nits[j] = in;
                outputs[i].output_nits[j] = out;
            }
            outputs[i].lut_len = steps + 1;
        }

        printf("\n");
    }

    toml_free(result);

    return monitors.u.arr.size;
}

void config_free(output_config *out) {
    free(out->input_nits);
    free(out->output_nits);
    free(out);
}