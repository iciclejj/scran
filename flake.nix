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
        default = client-1;
        client-1 = pkgs.callPackage ./default.nix { };
      };
    };
}
