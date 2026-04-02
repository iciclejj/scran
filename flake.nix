{
  inputs = {
    nixpkgs.url = "github:/nixos/nixpkgs/nixos-unstable";
  };

  outputs =
    {self, ...}@inputs:
    let
      system = "x86_64-linux";
      pkgs = import inputs.nixpkgs { inherit system; };
    in
    {
      packages."${system}" = rec {
        default = scran;
        scran = release;

        release = pkgs.callPackage ./default.nix { _target = "release"; };
        debug = pkgs.callPackage ./default.nix { _target = "debug"; };
      };
      devShells."${system}" = {
        default = pkgs.callPackage ./shell.nix { };
      };
    };
}
