{ pkgs, ... }:

# let
#   pkg = pkgs.callPackage ./default.nix { };
# in
(pkgs.callPackage ./default.nix { }).overrideAttrs {
  hardeningDisable = [ "all" ];
}
