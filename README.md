# Scran
#### ⚠️  Work In Progress  ⚠️
Capture images and videos. Only tested on [sway](https://swaywm.org/).

## Installing (Nix)

Example (flake coming soon):

```nix
let
  scran_src = builtins.fetchGit {
    url = "https://github.com/iciclejj/scran";
    ref = "main";
    rev = ""; # Desired commit
  };

  scran = pkgs.callPackage scran_src { };
in
{
  environment.systemPackages = [
    scran
  ];
}
```


## Usage & Behavior
Images and videos are saved to the file or directory specified by `output_path`, or to `/tmp/scran-capture/scran-<timestamp>.<file-extension>` by default.

Images are also sent to the clipboard.

NOTE: Video capture uses CPU encoding at the moment. GPU/hardware-acceleration coming soon.

### Sway config examples
```bash # works well enough...
# Launch scran
bindsym Print          exec  scran
# Grab focus (after releasing with Tab):
bindsym Shift+Alt+Tab  exec 'pkill -SIGUSR1 scran'

# For similar behavior to   'grim -g "$(slurp -d)" - | satty -f -'
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
         Example: 'scran -B - | satty -f -'
          By default, scran stays alive after exit to manage the clipboard
         (until another process takes over, e.g. you copied some text in a web
         browser). Useful if you want to pipe scran's output to an application
         that is waiting for scran to fully exit.
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
