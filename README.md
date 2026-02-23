# ⚠️ Work In Progress ⚠️
# Scran
Screen capture. Only tested on [sway](https://swaywm.org/).

## Installing (Nix)

Simple example (many ways to do it):

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

Sway config example:
```
bindsym Print exec scran
```

## Usage & Behavior
Image/video is saved to current directory.

Image also sent to clipboard

### Keymap (as of v0.1.0)
- Left mouse button
  - Init selection
  - After init: Toggle move selection
- Right mouse button
  - Toggle resize selection
- Enter
  - Capture image and exit
      - Stays alive in the background to handle clipboard. (Optional integration with external clipboard managers is planned.)
- Shift + Enter
  - Capture image
- Space
  - Capture video (start/stop)
- Escape
  - Stop video capture
  - Exit

## Primary Feature-TODOs
- VA-API
- Low-hanging fruit optimization
- Improved multi-display support
- Configuration (config file and/or cli args)
  - Keybindings
  - UX customization flags
    - Instant exit after capture
    - Click-and-hold vs click-to-toggle selection movement
    - etc.
  - Output-filenames
  - ?
- slurp/grim-compat
  - Outputting slurp-style geometry string
  - Consuming slurp-style geometry string
- Probably some more things I'm forgetting
