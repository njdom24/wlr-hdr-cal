{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    wayland
    wayland-scanner
    pkg-config
    gcc
    meson
    ninja
  ];
}