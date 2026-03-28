# Scran
Capture images and videos.

Scran is still under heavy development. Please open an issue if you find any bugs!

Inspired by [grim](https://wayland.emersion.fr/grim/) and [slurp](https://wayland.emersion.fr/slurp/).

<details>
<summary>Demo</summary>

https://github.com/user-attachments/assets/66b221d4-c070-4f03-a44f-142a6e5216b2
</details>

### Compositor support:


- Supported:
  - [Sway](https://swaywm.org/)
  - [COSMIC](https://system76.com/cosmic)
- Not supported:
  - KWin (KDE Plasma)
  - Mutter (Gnome)

<details> <summary>

#### Notes on compositor support
</summary>

Whether a compositor is supported mainly depends on whether it implements the
requried Wayland protocols. Most currently-unsupported compositors are only
missing the `ext_image_copy_capture`/`ext_image_capture_source` protocol pair.

A current list of compositors implementing this protocol can be found at
https://wayland.app/protocols/ext-image-copy-capture-v1#compositor-support.

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

### Nix flake

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

<details> <summary>

### Non-flake Nix example
</summary>

```nix
let
  scran_src = builtins.fetchGit {
    url = "https://github.com/iciclejj/scran";
    ref = "main";
    # Desired commit (v0.2.2 here)
    rev = "83d70c404c1df1a61e1717b427f2cec597df5016";
  };

  scran = pkgs.callPackage scran_src { };
in
{
  environment.systemPackages = [
    scran
  ];
}
```
</details>

<details> <summary>

### Arch, Ubuntu etc., without Nix
</summary>

1. **Install Dependencies**
   <details open> <summary>Arch</summary>

   ```bash
   pacman -S   base-devel wayland wayland-protocols libxkbcommon ffmpeg
   # Install Blend2D through the AUR (See below if you prefer to build Blend2D manually.)
   yay -S blend2d
   ```
   </details>
   <details> <summary>Ubuntu</summary>

   ```bash
   apt install make gcc pkg-config libwayland-dev wayland-protocols libxkbcommon-dev libavcodec-dev libavutil-dev libavformat-dev libavfilter-dev
   ```
   </details>
   <details> <summary>Fedora</summary>

   #### Fedora:
   Warning: Personally tested Fedora builds are failing to video correctly, at the moment. The bug
            can likely be worked around by using a different ffmpeg build than the
            one I was linking against (installed through the below command).
            Your mileage may vary.
            
   ```bash
   dnf install make gcc pkg-config wayland-devel wayland-protocols-devel libxkbcommon-devel libavcodec-free-devel libavutil-free-devel libavformat-free-devel libavfilter-free-devel blend2d-devel
   ```
   </details>

   Note: The libavcodec version installed by your package manager may or may not
         be built with GPL-licensed video encoders such as libx264. scran will
         pick from whatever is available.

   <!--
   TODO: Consider adding blend2d as a git submodule.
   -->
   [See instructions below](#blend2d) if Blend2D is not packaged for your distribution.
2. **Build**
   ```bash
   git clone "https://github.com/iciclejj/scran"
   cd scran
   make -j release
   ```

3. **Install**
   ```bash
   # scran should now be at ./build/release/scran.

   # To install it system-wide (may require sudo):
   # Assuming your distro expects installs to /usr/local/bin/:
   install -m 755 ./build/release/scran -D /usr/local/bin/scran
   ```

#### Blend2d
If Blend2D was not packaged for your package manager (e.g. Ubuntu), you can
compile and install it manually prior to building scran. It should not take
very long to build:
<details> <summary>Build Instructions</summary>

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
Images and videos are saved to the file or directory specified by `output_directory`,
or to `/tmp/scran-capture/scran-<timestamp>.<file-extension>` by default.
Saved images and videos are also sent to the clipboard.

NOTE: The video capture pipeline is fully CPU-based at the moment.
GPU/hardware-acceleration is planned for after the CPU pipeline is
is more optimized (primarily improving performance for rotated displays).

See also `scran -h`.

### Sway config examples
Launch scran:
```bash # works well enough...
bindsym Print          exec  scran
```
Grab focus (after releasing with Tab):
```bash
bindsym Shift+Alt+Tab  exec 'pkill -SIGUSR1 scran'
```

If you want to pipe scran's output to another program, you might need to
prevent scran from staying alive to manage the clipboard clipboard. You
can do this by passing -B:
```bash
bindsym Print          exec 'scran -B - | satty -f -'
```
...or for similar behavior to `grim -g "$(slurp -d)" - | satty -f -`:
```bash
bindsym Print          exec 'scran -Be - | satty -f -'
```

### Keymap
```
  Left mouse button    Initialize and move selection
  Right mouse button   Resize selection
  Enter                Capture image and exit
                         Stays alive in the background to handle clipboard,
                         unless the -B option is provided.
  Shift+Enter          Capture image
  Space                Capture video (start/stop)
  Tab                  Release focus (stop capturing inputs)
                         SIGUSR1 to retake focus - see Signals section and
                         sway config examples.
  Escape               Exit scran, or stop video capture if in progress
```

### Command-line Arguments
See `scran -h` for more details
```
  Usage: scran [options...] [output_directory]

  output_directory   path to output directory
                       Directory will be created if it does not exist.
                       If set to -, scran writes to stdout (see also -B)

  -f   <filename_pattern>
         Name of the file that will be placed inside of `output_directory`
         Ignored if output_directory is - (stdout)
         Expanded patterns:
           %Y  Year  (4 digits)        %H  Hour         (00-23)
           %m  Month (01-12)           %M  Minute       (00-59)
           %d  Day   (01-31)           %S  Second       (00-59)
                                       %U  Microsecond  (000000-999999)
           %E  File extension (e.g. .png or .mp4)
           %%  A literal '%' character
  -p   press-only mouse buttons (presses toggle pressed/released state)
  -e   automatically capture and exit immediately after initial selection
  -B   do not keep background process alive
  -s   slurp: send selection as geometry string to standard output
         Equivalent to slurp's default output.
         See https://wayland.emersion.fr/slurp/.
  -g   "<x>,<y> <width>x<height>"
         Pre-initialize selection using slurp-style geometry string
  -h   show help message and exit
```

### Signals
Send SIGUSR1 to the running scran to start grabbing inputs again after releasing with Tab.
- Example: `pkill -SIGUSR1 scran`

## Primary Feature TODOs
Feel free to open a feature request even if something is already listed here.
- GPU-accelerated video capture
- More configuration
  - **Specify output file formats, encoding, etc.**
  - Customizable keybindings
  - Config file?
  - UI customization, for example:
    - Custom colors
    - Option to display current selection size
- Cross-display capture
    - Already handles separate simultaneous video capture per individual display
- Desktop notifications
