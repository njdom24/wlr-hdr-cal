#include <math.h>
#include <errno.h>
#include "bluelight.h"

// Blue light filter code inherited from wlsunset
/*
Copyright 2020 Kenny Levinsen

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

uint32_t bluelight_temperature = 6500;

// Illuminant D, or daylight locus.
static int illuminant_d(int temp, double *x, double *y) {
    if (temp >= 2500 && temp <= 7000) {
        *x = 0.244063 + 0.09911e3 / temp + 2.9678e6 / pow(temp, 2) - 4.6070e9 / pow(temp, 3);
    } else if (temp > 7000 && temp <= 25000) {
        *x = 0.237040 + 0.24748e3 / temp + 1.9018e6 / pow(temp, 2) - 2.0064e9 / pow(temp, 3);
    } else {
        errno = EINVAL;
        return -1;
    }
    *y = (-3 * pow(*x, 2)) + (2.870 * (*x)) - 0.275;
    return 0;
}

// Planckian locus.
static int planckian_locus(int temp, double *x, double *y) {
    if (temp >= 1667 && temp <= 4000) {
        *x = -0.2661239e9 / pow(temp, 3) - 0.2343589e6 / pow(temp, 2) + 0.8776956e3 / temp + 0.179910;
        if (temp <= 2222) {
            *y = -1.1064814 * pow(*x, 3) - 1.34811020 * pow(*x, 2) + 2.18555832 * (*x) - 0.20219683;
        } else {
            *y = -0.9549476 * pow(*x, 3) - 1.37418593 * pow(*x, 2) + 2.09137015 * (*x) - 0.16748867;
        }
    } else if (temp > 4000 && temp < 25000) {
        *x = -3.0258469e9 / pow(temp, 3) + 2.1070379e6 / pow(temp, 2) + 0.2226347e3 / temp + 0.240390;
        *y = 3.0817580 * pow(*x, 3) - 5.87338670 * pow(*x, 2) + 3.75112997 * (*x) - 0.37001483;
    } else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static double clamp(double value) {
    if (value > 1.0) return 1.0;
    if (value < 0.0) return 0.0;
    return value;
}

static bluelight_rgb xyz_to_gamma_rgb(double x, double y, double z) {
    // http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
    // Assumes 2.2 gamma (should match wlsunset)
    return (bluelight_rgb) {
        .r = pow(clamp(3.2404542 * x - 1.5371385 * y - 0.4985314 * z), 1.0 / 2.2),
        .g = pow(clamp(-0.9692660 * x + 1.8760108 * y + 0.0415560 * z), 1.0 / 2.2),
        .b = pow(clamp(0.0556434 * x - 0.2040259 * y + 1.0572252 * z), 1.0 / 2.2)
    };
}

bluelight_rgb bluelight_temp_to_rgb(uint32_t kelvin) {
    if (kelvin == 6500) {
        return (bluelight_rgb){.r = 1.0, .g = 1.0, .b = 1.0};
    }

    double x, y;
    if (kelvin >= 25000) {
        illuminant_d(25000, &x, &y);
    } else if (kelvin >= 4000) {
        illuminant_d(kelvin, &x, &y);
    } else if (kelvin >= 2500) {
        double x1, y1, x2, y2;
        illuminant_d(kelvin, &x1, &y1);
        planckian_locus(kelvin, &x2, &y2);
        double factor = (4000.0 - kelvin) / 1500.0;
        double sinefactor = (cos(M_PI * factor) + 1.0) / 2.0;
        x = x1 * sinefactor + x2 * (1.0 - sinefactor);
        y = y1 * sinefactor + y2 * (1.0 - sinefactor);
    } else {
        planckian_locus(kelvin >= 1667 ? kelvin : 1667, &x, &y);
    }

    double z = 1.0 - x - y;
    bluelight_rgb rgb = xyz_to_gamma_rgb(x, y, z);
    
    // rgb_normalize()
    double maxw = fmax(rgb.r, fmax(rgb.g, rgb.b));
    if (maxw > 0) {
        rgb.r /= maxw;
        rgb.g /= maxw;
        rgb.b /= maxw;
    }

    return rgb;
}

