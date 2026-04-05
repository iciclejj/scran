{
  stdenv,
  lib,
  pkg-config,
  wayland,
  wayland-scanner,
  wayland-protocols,
  blend2d,
  libxkbcommon,
  ffmpeg,
  basu, # libsystemd's sd-bus library
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
  version = "0.5.0";
  src = _src;

  nativeBuildInputs = [
    wayland-scanner
    wayland-protocols
    pkg-config
    copyDesktopItems
  ];

  buildInputs = [
    wayland
    blend2d
    libxkbcommon
    ffmpeg
    basu
  ];

  inherit hardeningDisable;
  buildFlags = [ _target ];

  installPhase = ''
    runHook preInstall
    install -D build/${_target}/scran $out/bin/scran
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

  meta = {
    description = "Sway screen capture";
    mainProgram = "scran";
    license = lib.licenses.mit;
    homepage = "https://github.com/iciclejj/scran";
  };
}
