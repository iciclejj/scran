# Scran
####  Work In Progress
Capture images and videos. Only tested on [sway](https://swaywm.org/).

## Installing

### Nix
Example (flake coming soon):

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

### Other distributions (without Nix)
1. **Install Dependencies**
   <!--
   TODO: Add proper ubuntu instructions. Also consider adding blend2d as a git submodule.
   -->
   #### Arch:
   ```bash
   pacman -S   base-devel wayland wayland-protocols libxkbcommon ffmpeg
   # Install Blend2D through the AUR (See below if you prefer to build Blend2D manually.)
   yay -S blend2d
   ```
   #### Fedora:
   Warning: Personally tested Fedora builds are failing to video correctly, at the moment. The bug
            can likely be worked around by using a different ffmpeg build than the
            one I was linking against (installed through the below command).
            Your mileage may vary.
            
   ```bash
   dnf install make gcc pkg-config wayland-devel wayland-protocols-devel libxkbcommon-devel libavcodec-free-devel libavutil-free-devel libavformat-free-devel libavfilter-free-devel blend2d-devel
   ```

   <br>
   Note: The libavcodec version installed by your package manager may or may not
         be built with GPL-licensed video encoders such as libx264. scran will
         pick from whatever is available.

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
<details>
<summary>Build Instructions</summary>

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
cmake --install build

# Now you can go back to the scran repo and build
```
</details>

## Usage
Images and videos are saved to the file or directory specified by `output_path`, or to `/tmp/scran-capture/scran-<timestamp>.<file-extension>` by default. Images are also sent to the clipboard.

NOTE: Video capture uses CPU encoding at the moment. GPU/hardware-acceleration coming soon.

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

### Keymap (as of v0.2.0)
For different versions, use `scran -h`.
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

### Options
```
  -p   press-only mouse buttons (presses toggle pressed/released state)
  -e   automatically capture and exit immediately after initial selection
  -B   do not keep background process alive
  -h   show help message and exit
```
### Positional arguments
```
  output_path   path to output file or directory.
                output_path is -:
                  -  scran writes to stdout (See also: -B)
                output_path is an existing directory:
                  -  scran writes to <output_path>/<default_filename>
                output_path does not exist, but ends with '/':
                  1. scran creates directory structure
                  2. scran writes to <output_path>/<default_filename>
                output_path does not exist:
                  1. scran creates directory structure if necessary
                  2. scran writes to <output_path>
                  NOTE: the *exact* given file path is used for both image and video
```

### Signals
Send SIGUSR1 to the running scran to start grabbing inputs again after releasing with Tab.
- Example: `pkill -SIGUSR1 scran`

## Primary Feature TODOs
Feel free to open a feature request even if something is already listed here.
- VA-API for video encoding
- More configuration
  - Customizable keybindings
  - Config file?
  - User-specified filename format (e.g. 'myscreenshot-%Y%m%d.%H:%M')
  - ..?
- slurp/grim compatibility mode
  - Outputting slurp-style geometry string
  - Consuming slurp-style geometry string
- Desktop notifications
- Cross-display capture
    - Already handles separate simultaneous video capture per individual display
- Slightly less bare-bones UI
  - Show current selection size
  - Highlight currently selected resize corner
  - ..?
