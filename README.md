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
  -h   show help message and exit
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
