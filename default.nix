{
  stdenv,
  lib,
  pkg-config,
  bintools,
  wayland,
  wayland-scanner,
  wayland-protocols,
  blend2d,
  libxkbcommon,
  ffmpeg,
  pipewire,
  basu, # libsystemd's sd-bus library
  copyDesktopItems,
  makeDesktopItem,
  meson,
  ninja,

  # src overridable to ease installation from outside of nixpkgs, among other
  # things
  _src ? ./.,

  _target ? "release",
  hardeningDisable ? if _target == "debug" then [ "all" ] else [ ],
}:

stdenv.mkDerivation {
  pname = "scran";
  version = "v0.9.2";
  src = _src;

  nativeBuildInputs = [
    wayland-scanner
    wayland-protocols
    pkg-config
    bintools
    meson
    ninja
    copyDesktopItems
  ];

  buildInputs = [
    wayland
    blend2d
    libxkbcommon
    ffmpeg
    pipewire
    basu
  ];

  inherit hardeningDisable;
  mesonBuildType = _target;

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
    license = [
      lib.licenses.mit
      lib.licenses.ofl
    ];
    homepage = "https://github.com/iciclejj/scran";
  };
}
