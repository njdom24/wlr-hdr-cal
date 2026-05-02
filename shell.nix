{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    wayland
    wayland-scanner
    wlr-protocols
    pkg-config
    gcc
    meson
    ninja
    tomlc17
  ];
}