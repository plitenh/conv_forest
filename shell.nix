{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  nativeBuildInputs = [ pkgs.pkg-config ];
  buildInputs = [
    pkgs.curl.dev
    pkgs.sqlite.dev
    pkgs.cjson
    pkgs.openssl.dev
    pkgs.tesseract
    pkgs.ydotool
    pkgs.grim
    pkgs.slurp
  ];
}
