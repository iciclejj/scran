{
  stdenv,
  pkg-config,
  wayland,
  wayland-scanner,
  wayland-protocols,
  wlr-protocols,
  blend2d
}:

stdenv.mkDerivation {
  pname = "client-1-test";
  version = "0.1.1";
  src = ./.;

  nativeBuildInputs = [
    wayland-scanner
    wlr-protocols
    wayland-protocols
    pkg-config
  ];

  buildInputs = [
    wayland
    blend2d
  ];

  installPhase = ''
    install -D main $out/bin/wayland-client-test-1
  '';
}
