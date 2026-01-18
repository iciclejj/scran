{
  stdenv,
  pkg-config,
  wayland,
  wayland-scanner,
  wayland-protocols,
  wlr-protocols,
  blend2d,
  libxkbcommon,
  ffmpeg,
  copyDesktopItems,
  makeDesktopItem,

  # src overridable to ease installation from outside of nixpkgs, among other
  # things
  src ? ./.,
}:

stdenv.mkDerivation {
  pname = "client-1-test";
  version = "0.1.1";
  inherit src;

  nativeBuildInputs = [
    wayland-scanner
    wlr-protocols
    wayland-protocols
    pkg-config
    copyDesktopItems
  ];

  buildInputs = [
    wayland
    blend2d
    libxkbcommon
    ffmpeg
  ];

  installPhase = ''
    runHook preInstall
    install -D build/release/main $out/bin/wayland-client-test-1
    runHook postInstall
  '';

  desktopItems = [
    # TODO: Change this
    (makeDesktopItem {
      name = "client-1-test";
      exec = "client-1-test";
      comment = "Screen capture...";
      desktopName = "Client-1-Test";
      genericName = "Client-1-Test";
    })
  ];
}
