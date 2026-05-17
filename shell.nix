{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    wayland
    wayland-scanner
    wayland-protocols
    wlr-protocols
    pkg-config
    gcc
    meson
    ninja
    tomlc17
    dbus
  ];
}