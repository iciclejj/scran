# Scran
Capture images and videos.

Scran is still under heavy development. Please open an issue if you find any bugs!

Inspired by [grim](https://wayland.emersion.fr/grim/) and [slurp](https://wayland.emersion.fr/slurp/).


https://github.com/user-attachments/assets/bee48b04-f073-48f1-a37f-511e5d7c8426

### Compositor support:

- Supported:
  - [Sway](https://swaywm.org/)
  - [Hyprland](https://hypr.land/)
  - [COSMIC](https://system76.com/cosmic)
- Not supported:
  - KWin (KDE Plasma)
  - Mutter (Gnome)

<details> <summary>

#### <ins>Notes on compositor support</ins>
</summary>

Whether a compositor is supported mainly depends on whether it implements the
required Wayland protocols. Most currently-unsupported compositors are only
missing the `ext_image_copy_capture`/`ext_image_capture_source` protocol pair.

A list of compositors implementing this protocol can be found at
https://wayland.app/protocols/ext-image-copy-capture-v1#compositor-support.
NOTE: This website is not automatically updated. If the protocols were only
recently implemented for your compositor, then it might still be incorrectly
listed as unsupported.

As of today, the only reasonable way of supporting most of the remaining
compositors, to my knowledge, would be to implement screen capture through
D-Bus, through XDG Desktop Portals (Screenshot and ScreenCast portals).
For video (ScreenCast portal), this would also mean going through PipeWire.

I do not have any immediate plans to support capturing through
XDG Desktop Portals. If your compositor adds support for the above-mentioned
`ext_image_*` protocols, then Scran will likely start working immediately.
Any potentially remaining incompatibilities should at least be much, much
simpler to fix.
</details>

## Installing


<details open> <summary>

### <ins>Arch (AUR)</ins>
</summary>

```bash
yay -S scran
```

<details> <summary>

### <ins>Nix</ins>
</summary>

### Flake

> `nix run "github:iciclejj/scran"` to try it out without installing.

Add the flake to your your NixOS or home-manager flake inputs
```nix
inputs = {
  scran.url = "github:iciclejj/scran";
  # ...
};
```

Then you can install it like so:
```nix
# For home-manager, use home.packages = [ ... ];
systemPackages = [
  inputs.scran.packages.x86_64-linux.scran
];
```
Other architectures have not been tested, and so are not in the flake.

### Without flakes

```nix
let
  scran_src = builtins.fetchGit {
    url = "https://github.com/iciclejj/scran";
    ref = "main";
    # Desired commit (v0.7.0 here)
    rev = "c3e55d090a1bf8c4830ebf806143ac5f17b9a4e7";
    sha256 = "11xbh6iyj8b7hrc066c7gky9y80zk4wrlkbq7m1q0vy5gqsvfpw0";
  };

  scran = pkgs.callPackage scran_src { };
in
{
  # For home-manager, use home.packages = [ ... ];
  environment.systemPackages = [
    scran
  ];
}
```
</details>

<details> <summary>

### <ins>Building and installing manually</ins>
</summary>

1. **Install Dependencies**

   > `libsystemd`/`systemd-devel` is only used for sd-bus, and can safely be
     replaced with [`basu`](https://sr.ht/~emersion/basu/) if you're not using
     systemd and do not wish to pull in all of libsystemd. (Arch users can get
     it from the AUR.)

   > The libavcodec version installed by your package manager may or may not
     be built with GPL-licensed video encoders such as libx264. scran will
     pick from whatever is available.

   > Audio capture requires PipeWire at runtime. Missing PipeWire is handled
     gracefully, with a printed WARNING message.

   <details open> <summary><ins>Arch</ins></summary>

   ```bash
   pacman -S base-devel meson ninja wayland wayland-protocols libxkbcommon libsystemd libpipewire ffmpeg
   # Install Blend2D through the AUR (See below if you prefer to build Blend2D manually.)
   yay -S blend2d
   ```
   </details>
   <details> <summary><ins>Ubuntu</ins></summary>

   ```bash
   apt install meson ninja-build gcc pkg-config libwayland-dev wayland-protocols libxkbcommon-dev libsystemd-dev libpipewire-0.3-dev libavcodec-dev libavutil-dev libavformat-dev
   ```
   </details>
   <details> <summary><ins>Fedora</ins></summary>

   ```bash
   dnf install meson ninja gcc pkg-config wayland-devel wayland-protocols-devel libxkbcommon-devel systemd-devel pipewire-devel libavcodec-free-devel libavutil-free-devel libavformat-free-devel blend2d-devel
   ```
   </details>

   <!--
   TODO: Consider adding blend2d as a git submodule.
   -->
   [See instructions below](#blend2d) if Blend2D is not packaged for your distribution.

2. **Build**
   ```bash
   git clone "https://github.com/iciclejj/scran"
   cd scran
   meson setup build --buildtype=release
   meson compile -C build
   ```

3. **Install**
   ```bash
   # scran should now be at ./build/scran.
   # To install it system-wide (may require sudo):
   meson install -C build
   ```

#### Blend2d
If Blend2D was not packaged for your package manager (e.g. Ubuntu), you can
compile and install it manually prior to building scran. It should not take
very long to build:
<details> <summary><ins>Build Instructions</ins></summary>

```bash
# First install cmake and g++ through your package manager.
# For Ubuntu:
apt install cmake g++

# Clone blend2d and asmjit. You could do this within scran git directory (from step 2 above).
git clone https://github.com/blend2d/blend2d
git clone https://github.com/asmjit/asmjit blend2d/3rdparty/asmjit

cd blend2d

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Install blend2d system-wide (may require sudo):
cmake --install build && ldconfig

# Now you can go back to the scran repo and build
```
</details>
</details>

## Usage

### Keymap
```
  Left mouse button    Initialize and move selection
  Right mouse button   Resize selection

  Enter                Capture image and exit
                         Stays alive in the background to handle clipboard,
                         unless the -B option is provided.
  Shift+Enter          Capture image

  Space                Capture video with audio
  Shift+Space          Capture video without audio

  Arrow keys           Move selection by one pixel

  Tab                  Release focus (stop capturing inputs)
                         Click tray icon or send SIGUSR1 to scran to retake focus.
                         See Signals section and config examples.

  Escape               Exit scran, or stop video capture if in progress
```

Images and videos are saved to the file or directory specified by `output_directory`,
or to `/tmp/scran-capture/scran-<timestamp>.<file-extension>` by default.
Saved images and videos are also sent to the clipboard.

See also `scran -h`.

<details> <summary>

### <ins>Sway config examples</ins>
</summary>
<ul>

Launch scran:
```bash # works well enough...
bindsym Print          exec  'scran'
```
... or with custom directory (-d directory must exist)
```bash
bindsym Print          exec  'scran -d "$HOME/Pictures/"'
```
Grab focus (after releasing with Tab):
```bash
bindsym Shift+Alt+Tab  exec 'pkill -SIGUSR1 scran'
```
</details>
</ul>

<details> <summary>

### <ins>Hyprland config examples</ins>
</summary>
<ul>

Launch scran:
```bash # works well enough... kinda. github doesn't seem to support hypr/hyprlang.
bind =          , print, exec, scran
```
... or with custom directory (-d directory must exist)
```bash
bind =          , print, exec, scran -d "$HOME/Pictures/"
```
Grab focus (after releasing with Tab):
```bash
bind = ALT SHIFT, tab  , exec, pkill -SIGUSR1 scran
```
</details>
</ul>


If you want to pipe scran's output to another program, you might need to
prevent scran from staying alive to manage the clipboard. You can do this
by passing -B:
```bash
scran -B - | satty -f -
```
... or for similar behavior to `grim -g "$(slurp -d)" - | satty -f -`:
```bash
scran -Be - | satty -f -
```


### Command-line Arguments
See `scran -h` for more details
```
  Usage: scran [options...] [output_directory]

  output_directory   path to output directory, or - (a hyphen) to write to stdout
                        Directory will be created if it does not exist.
                        See also -B if writing to stdout.
                        NOTE:
                          Other than "- (a hyphen) to write to stdout", the rest
                          of this convenience argument's behavior is still subject
                          to change. Please use -f, -d and SCRAN_OUTPUT_DIR if you
                          need stable commands for keybindings or scripts.

  -f   <filename_pattern>
         Name of the file that will be placed in the output directory
         Ignored if `output_directory` is - (stdout)
         Expanded patterns:
           %Y  Year  (4 digits)        %H  Hour         (00-23)
           %m  Month (01-12)           %M  Minute       (00-59)
           %d  Day   (01-31)           %S  Second       (00-59)
                                       %U  Microsecond  (000000-999999)
           %E  File extension (e.g. .png or .mp4)
           %%  A literal '%' character
  -d   set an existing directory as output directory
         You may also use $SCRAN_OUTPUT_DIR.
  -p   press-only mouse buttons (presses toggle pressed/released state)
  -e   automatically capture and exit immediately after initial selection
  -A   disable audio capture (during video capture)
         Note: audio capture requires PipeWire.
  -C   disable cursor capture
  -B   do not keep background process alive
  -z   freeze the display at startup
  -s   slurp: send selection as geometry string to standard output
         Equivalent to slurp's default output.
         See https://wayland.emersion.fr/slurp/.
  -g   "<x>,<y> <width>x<height>"
         Pre-initialize selection using slurp-style geometry string
  -N   disable notifications
  -U   hide UI (partial)
  -UU  hide UI (full)
  -v   show version and exit
  -h   show help message and exit
```

### Signals
Send SIGUSR1 to the running scran to start grabbing inputs again after releasing with Tab.
- Example: `pkill -SIGUSR1 scran`

## TODOs (*not comprehensive*)
- GPU-accelerated video capture
  - planned for after the CPU pipeline is more optimized (primarily improving performance for rotated displays).
- More configuration
  - Specify output file formats, encoding, etc.
  - Customizable keybindings
    - Config file?
  - UI customization, for example:
    - Custom colors
    - Option to display current selection size
- Cross-display capture
    - Already handles separate simultaneous video capture per individual display

**Feel free to open a feature request, even if something is already listed here.**
