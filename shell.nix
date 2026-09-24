{ pkgs ? import <nixpkgs> {} }:

with pkgs; mkShell {
  packages = [ arduino-cli gnumake ];

  shellHook = ''
    export NIX_SHELL_NAME="ARDUINO"
  '';
}
