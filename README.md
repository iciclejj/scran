# Scran
#### ⚠️  Work In Progress  ⚠️
Screen capture. Only tested on [sway](https://swaywm.org/).

## Installing (Nix)

Example (flake coming soon):

```nix
let
  scran_src = builtins.fetchGit {
    url = "https://github.com/iciclejj/scran";
    ref = "main";
    # Desired commit (v0.1.0-beta)
    rev = "58e4fad53de6c9c229d5e3c40ce32e5f744006e0";
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
Image/video is saved to directory specified with `-d`, or `/tmp/scran-capture/` by default.

Image also sent to clipboard

### Options
```
  -d   directory path for output files
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
### Keymap (as of v0.2.0)
For different versions, use `scran -h`.
- **Left mouse button**
  - Init selection
  - After init: Toggle move selection
- **Right mouse button**
  - Toggle resize selection
- **Enter**
  - Capture image and exit
      - Stays alive in the background to handle clipboard. (Optional integration with external clipboard managers is planned.)
- **Shift + Enter**
  - Capture image
- **Space**
  - Capture video (start/stop)
- **Escape**
  - Stop video capture
  - Exit
- **Tab (New!)**
  - Release focus
    - See [Sway config example](#sway-config-example) and [Signals](#signals)

### Sway config example:
```
# Launch scran
bindsym Print         exec scran
# Grab focus (after releasing with <Tab>):
bindsym Shift+Alt+Tab exec 'pkill -SIGUSR1 scran'
```

### Signals
Send SIGUSR1 to the running scran to start grabbing inputs again after releasing with <Tab>.
- Example: `pkill -SIGUSR1 scran`

## Primary Feature-TODOs
- VA-API
- More configuration
  - Customizable keybindings
  - ..?
- slurp/grim compatibility mode
  - Outputting slurp-style geometry string
  - Consuming slurp-style geometry string
- Cross-display capture
    - Already handles separate simultaneous video capture per individual display
