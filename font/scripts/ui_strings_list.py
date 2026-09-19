# Nerd Fonts uses Font Awesome's Private Use Area codepoints.
FONT_AWESOME_SPEAKER_OFF = "\uf026"
FONT_AWESOME_SPEAKER_ON = "\uf028"
FALLBACK_GLYPH = "�"
FONT_AWESOME_PUA_CODEPOINTS = {
    ord(FONT_AWESOME_SPEAKER_OFF),
    ord(FONT_AWESOME_SPEAKER_ON),
}


UI_STRINGS = {
    "greeting":                        "Fullscreen capture · click and drag anywhere for custom selection",
    "keymap_image_default":            "[↵] Image & Exit",
    "keymap_image_mod":                "[↵] Image       ",
    "keymap_video_default":            f"[␣] Video {FONT_AWESOME_SPEAKER_ON}",
    "keymap_video_mod":                f"[␣] Video {FONT_AWESOME_SPEAKER_OFF}",
    "keymap_focus_default":            "[⇥] Release focus",
    "keymap_focus_released_tray":      "[⇥] Click tray icon to retake focus.",
    "keymap_focus_released_help":      "[⇥] Focus released. 'scran -h' for help.",
    "keymap_freezeframe_turn_on":      "[Z] Freeze screens",
    "keymap_freezeframe_turn_off":     "[Z] Unfreeze screens",

    # Placeholders for calculating metadata (currently only maximum pixel
    # widths); the displayed strings are generated dynamically.
    "statusline_selection_size_dummy": "WWWWWxHHHHH",
    "statusline_timer_dummy":          "00:00:00",

    # Just for filling the atlas:
    "atlas_digits":                    "0123456789",
    "atlas_separators":                ":x",
    "fallback_glyph":                  FALLBACK_GLYPH,

    # Ensure the dynamic strings have a space
    "space":                           " ",

    "empty":                           "",
}
