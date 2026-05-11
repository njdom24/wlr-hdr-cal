# wlr-hdr-cal

Tool to calibrate and adjust the brightness of HDR outputs for compositors implementing the `wlr-gamma-control-unstable-v1` protocol.

## Features
- **EOTF Curve Shaper**: Applies custom PQ gamma ramps to fine-tune the HDR brightness curve, allowing you to manipulate how bright specific nits levels are rendered.
- **HDR Detection**: Automatically enables/disables when an output enters/leaves HDR mode. (e.g. `swaymsg output DP-1 hdr {on/off}` in Sway)
- **TOML Configuration**: Easy-to-use configuration file for mapping specific monitors by name, applying simple brightness multipliers like KDE Plasma's calibration tool, or setting up complex piecewise-linear LUTs.

## Dependencies
- `wlr-protocols`
- `libtomlc17`

Repo packages from your distro are preferred, but these dependencies are available as submodules for distros lacking them.

## Building
You can build the project using Meson:

```sh
meson setup build
ninja -C build
```
*Nix users: You can use `nix-shell` to enter an environment with the necessary build dependencies. Supports `direnv` to ease development / testing*

## Configuration
The config is a TOML file read from `~/.config/wlr-hdr-cal/config`.

### Example Configuration
```toml
[[monitors]]
name = "AOC Q27G40XMN" # Match by make + model
multiplier = 0.5 # Halve brightness across the board

[[monitors]]
name = "DP-2" # Match by connector name

# Brighten low-mid range, then scale linearly without clipping
values = [
  [0, 0],
  [100, 200],
  [200, 400],
  [10000, 10000]
]

[[monitors]]
name = "HDMI-A-1"

# Same as DP-2, but 20% brighter (e.g. for living room TV)
multiplier = 1.2
values = [
  [0, 0],
  [100, 200],
  [200, 400],
  [10000, 10000]
]
```

### Monitor Options
- `name`: The connector name of the Wayland output (e.g., `DP-1`, `HDMI-A-1`).
- `multiplier` (optional, default `1.0`): A linear multiplier to uniformly apply to the output brightness.
  - A **1x** multiplier is similar to setting **203 nits** as the comfortable brightness level in KDE's calibration tool, and **2x** is similar to **406 nits**.
- `values` (optional): For more advanced calibration. List of `[input_nits, output_nits]` pairs defining a PQ EOTF mapping input brightness to a preferred output brightness. Useful for working around displays with inconsistent / wonky EOTF curves.
  - The first coordinate (`input_nits`) must be strictly increasing across the array.
  - The ends of the curve are automatically clamped to `0` and `10000`.
  - If a `multiplier` is also defined, the defined `output_nits` will be multiplied..
