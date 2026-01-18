{ pkgs, ... }:

pkgs.callPackage ./default.nix { _target = "debug"; }
