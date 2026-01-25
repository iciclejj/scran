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
  _src ? ./.,

  _target ? "release",
  hardeningDisable ? if _target == "debug" then [ "all" ] else [ ],
}:

stdenv.mkDerivation {
  pname = "scran";
  version = "0.1.0-beta";
  src = _src;

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

  inherit hardeningDisable;
  buildFlags = [ _target ];

  installPhase = ''
    runHook preInstall
    install -D build/release/main $out/bin/scran
    runHook postInstall
  '';

  desktopItems = [
    # TODO: Change this
    (makeDesktopItem {
      name = "scran";
      exec = "scran";
      comment = "Screen capture";
      desktopName = "Scran";
      genericName = "Scran";
    })
  ];
}
