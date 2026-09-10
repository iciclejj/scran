#!/usr/bin/env python3
"""Generate Scran's UI font from its XZ-compressed source fonts."""

import argparse
import io
import lzma
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path
from textwrap import dedent

from fontTools import subset
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont
from fontTools.ttLib.scaleUpem import scale_upem

if __package__:
    from .ui_strings_list import (FONT_AWESOME_PUA_CODEPOINTS, UI_STRINGS,)
else:
    from ui_strings_list import (FONT_AWESOME_PUA_CODEPOINTS, UI_STRINGS,)


IOSEVKA_FAMILY_NAME = "Iosevka Fixed"

NERD_FONT_FAMILY_NAME = "Symbols Nerd Font Mono"

SCRAN_UI_FAMILY_NAME     = "Scran UI"
SCRAN_UI_SUBFAMILY_NAME  = "Regular"
SCRAN_UI_POSTSCRIPT_NAME = "ScranUI-Regular"

TRUETYPE_TIMESTAMP_1970 = 2082844800  # TrueType timestamps use a 1904 epoch.


@dataclass
class UICodepoints:
    iosevka: set[int]
    nerd_font: set[int]

    @property
    def combined(self) -> set[int]:
        return self.iosevka | self.nerd_font


def format_codepoints(codepoints: Iterable[int]) -> str:
    return ", ".join(f"U+{codepoint:04X}" for codepoint in sorted(codepoints))


def split_ui_codepoints_by_font() -> UICodepoints:
    ui_codepoints = {
        ord(character) for ui_string in UI_STRINGS.values() for character in ui_string
    }

    nerd_font_codepoints = ui_codepoints & FONT_AWESOME_PUA_CODEPOINTS
    iosevka_codepoints = ui_codepoints - nerd_font_codepoints

    unused_font_awesome_codepoints = FONT_AWESOME_PUA_CODEPOINTS - nerd_font_codepoints
    if unused_font_awesome_codepoints:
        print(
            "Ignoring Font Awesome PUA codepoints not used in any UI strings:",
            format_codepoints(unused_font_awesome_codepoints),
        )

    return UICodepoints(iosevka=iosevka_codepoints, nerd_font=nerd_font_codepoints)


def get_best_cmap(font: TTFont) -> dict[int, str]:
    cmap = font.getBestCmap()
    if cmap is None:
        raise ValueError("Unicode cmap subtable not found")
    return cmap


def load_xz_font(path: Path) -> TTFont:
    font_bytes = lzma.decompress(path.read_bytes())
    return TTFont(io.BytesIO(font_bytes), recalcTimestamp=False)


def validate_source_font(
    font: TTFont,
    expected_family_name: str,
) -> None:
    if "glyf" not in font:
        raise ValueError(f"{expected_family_name} must use TrueType outlines")

    if "fvar" in font:
        raise ValueError(f"{expected_family_name} must be a static font")

    if "name" not in font:
        raise ValueError(f"{expected_family_name} has no naming table")

    name_table = font["name"]

    family_name = (
        name_table.getDebugName(16)  # Typographic family
        or name_table.getDebugName(1)  # Legacy family
    )
    subfamily_name = (
        name_table.getDebugName(17)  # Typographic subfamily
        or name_table.getDebugName(2)  # Legacy subfamily
    )

    if family_name is None or family_name.casefold() != expected_family_name.casefold():
        raise ValueError(f"expected font family {expected_family_name!r}, found {family_name!r}")

    if subfamily_name is None or subfamily_name.casefold() != SCRAN_UI_SUBFAMILY_NAME.casefold():
        raise ValueError(
            f"expected font subfamily {SCRAN_UI_SUBFAMILY_NAME!r} for"
            + f" {expected_family_name!r}, found {subfamily_name!r}"
        )


def subset_font(
    font: TTFont,
    requested_codepoints: set[int],
    source_name: str,
) -> None:
    available_codepoints = set(get_best_cmap(font))
    missing_codepoints = requested_codepoints - available_codepoints
    if missing_codepoints:
        raise ValueError(f"{source_name} is missing: {format_codepoints(missing_codepoints)}")

    subsetter = subset.Subsetter(
        options=subset.Options(
            layout_features=[],
            name_IDs=["*"],
            name_languages=["*"],
            name_legacy=True,
        )
    )
    subsetter.populate(unicodes=requested_codepoints)
    subsetter.subset(font)


def vertical_metrics(font: TTFont) -> tuple[int, ...]:
    head = font["head"]
    hhea = font["hhea"]
    os2 = font["OS/2"]
    return (
        head.unitsPerEm,
        hhea.ascent,
        hhea.descent,
        hhea.lineGap,
        os2.sTypoAscender,
        os2.sTypoDescender,
        os2.sTypoLineGap,
    )


def import_truetype_glyphs(
    src_font: TTFont,
    dst_font: TTFont,
    codepoints: set[int],
) -> None:
    scale_upem(src_font, dst_font["head"].unitsPerEm)

    src_cmap = get_best_cmap(src_font)
    src_glyphs = src_font.getGlyphSet()
    src_hmtx = src_font["hmtx"]

    dst_glyf = dst_font["glyf"]
    dst_hmtx = dst_font["hmtx"]
    dst_cmaps = [
        table.cmap for table in dst_font["cmap"].tables if table.isUnicode()
    ]

    # Sort for reproducibility
    for codepoint in sorted(codepoints):
        glyph_name = src_cmap[codepoint]

        if glyph_name in dst_glyf:
            raise ValueError(f"destination glyph already exists: {glyph_name}")

        # Some glyphs reuse other glyphs as components. This inlines the
        # required outlines into the target glyph, avoiding the need to copy
        # over the component glyphs.
        outline_pen = DecomposingRecordingPen(src_glyphs)
        src_glyphs[glyph_name].draw(outline_pen)

        # Encode the recorded outline as a TrueType glyph
        glyph_pen = TTGlyphPen(None)
        outline_pen.replay(glyph_pen)

        dst_glyf[glyph_name] = glyph_pen.glyph()
        # Advance width and left side bearing:
        dst_hmtx[glyph_name] = src_hmtx[glyph_name]

        for cmap in dst_cmaps:
            cmap[codepoint] = glyph_name


def collect_source_name_records(fonts: Iterable[TTFont]) -> dict[int, str]:
    source_name_records = {}
    source_name_ids = (
        0,   # Copyright notice
        8,   # Manufacturer
        9,   # Designer
        10,  # Description
        13,  # License description
        14,  # License information URL
    )

    for name_id in source_name_ids:
        values = [
            value
            for font in fonts
            if (value := font["name"].getDebugName(name_id)) is not None
        ]
        if values:
            source_name_records[name_id] = "\n".join(dict.fromkeys(values))

    return source_name_records


def set_scran_ui_font_metadata(font: TTFont, source_name_records: dict[int, str]) -> None:
    name_table = font["name"]
    replacement_name_records = {
        **source_name_records,
        1:  SCRAN_UI_FAMILY_NAME,                         # Legacy family
        2:  SCRAN_UI_SUBFAMILY_NAME,                      # Legacy subfamily
        3:  f"{SCRAN_UI_FAMILY_NAME} {SCRAN_UI_SUBFAMILY_NAME} 1.0",  # Unique font identifier
        4:  f"{SCRAN_UI_FAMILY_NAME} {SCRAN_UI_SUBFAMILY_NAME}",      # Full font name
        5:  "Version 1.0",                                # Version string
        6:  SCRAN_UI_POSTSCRIPT_NAME,                     # PostScript name
        16: SCRAN_UI_FAMILY_NAME,                         # Typographic family
        17: SCRAN_UI_SUBFAMILY_NAME,                      # Typographic subfamily
        21: SCRAN_UI_FAMILY_NAME,                         # WWS family
        22: SCRAN_UI_SUBFAMILY_NAME,                      # WWS subfamily
    }
    for name_id, value in replacement_name_records.items():
        name_table.removeNames(nameID=name_id)
        # Windows Unicode BMP encoding, US English.
        name_table.setName(value, name_id, platformID=3, platEncID=1, langID=0x0409)

    # Fixed timestamps for reproducibility
    font["head"].created = TRUETYPE_TIMESTAMP_1970
    font["head"].modified = TRUETYPE_TIMESTAMP_1970


def validate_generated_font(
    font: TTFont,
    ui_codepoints: UICodepoints,
    iosevka_vertical_metrics: tuple[int, ...],
) -> None:
    cmap = get_best_cmap(font)
    if set(cmap) != ui_codepoints.combined:
        raise ValueError("generated font has unexpected cmap coverage")

    if vertical_metrics(font) != iosevka_vertical_metrics:
        raise ValueError(f"generated font changed {IOSEVKA_FAMILY_NAME}'s vertical metrics")

    hmtx = font["hmtx"]
    iosevka_advance_widths = {
        hmtx[cmap[codepoint]][0]
        for codepoint in ui_codepoints.iosevka
    }
    if len(iosevka_advance_widths) > 1:
        raise ValueError(f"{IOSEVKA_FAMILY_NAME} UI glyphs are not fixed-width")


def generate_font(
    iosevka_source_path: Path,
    nerd_font_source_path: Path,
) -> tuple[bytes, int]:
    # Iosevka is used as our base font.
    # TODO: Possibly generalize this a bit if we end up adding more icon fonts.

    ui_codepoints = split_ui_codepoints_by_font()

    with (
        load_xz_font(iosevka_source_path) as iosevka_font,
        load_xz_font(nerd_font_source_path) as nerd_font,
    ):
        # Suppress "PfEd NOT subset; don't know how to subset; dropped"
        for font in (iosevka_font, nerd_font):
            if "PfEd" in font:
                del font["PfEd"]

        validate_source_font(iosevka_font, IOSEVKA_FAMILY_NAME)
        validate_source_font(nerd_font, NERD_FONT_FAMILY_NAME)

        # Iosevka (our base font) defines the combined font's line metrics
        iosevka_vertical_metrics = vertical_metrics(iosevka_font)
        source_name_records = collect_source_name_records((iosevka_font, nerd_font))

        # Discard codepoints we don't use
        subset_font(iosevka_font, ui_codepoints.iosevka, IOSEVKA_FAMILY_NAME)
        subset_font(nerd_font, ui_codepoints.nerd_font, NERD_FONT_FAMILY_NAME)

        import_truetype_glyphs(nerd_font, iosevka_font, ui_codepoints.nerd_font)
        validate_generated_font(iosevka_font, ui_codepoints, iosevka_vertical_metrics)

        set_scran_ui_font_metadata(iosevka_font, source_name_records)

        font_buffer = io.BytesIO()
        iosevka_font.save(font_buffer)

        return font_buffer.getvalue(), len(ui_codepoints.combined)


def generate_c_header() -> str:
    enumerations = ',\n'.join(
        "    SCRAN_UI_TEXT_{}".format(key.upper()) for key in UI_STRINGS
    )

    return dedent(
        """\
        // Generated by generate_font.py. Do not edit.
        #ifndef SCRAN_GENERATED_FONT_H
        #define SCRAN_GENERATED_FONT_H

        #include <stddef.h>

        extern const unsigned char scran_font_ttf[];
        extern const size_t scran_font_ttf_size;

        enum scran_ui_text {{
        {enumerations},
            SCRAN_UI_N_TEXTS
        }};

        #endif
        """
    ).format(enumerations=enumerations)


def as_c_utf16_string_literal(string: str) -> str:
    c_literals = []

    for character in string:
        codepoint = ord(character)

        if character == "\\":
            c_literals.append("\\\\")
        elif character == '"':
            c_literals.append('\\"')
        elif codepoint < 0x20 or 0x7F <= codepoint < 0xA0:
            raise ValueError(f"control character U+{codepoint:04X} in UI string")
        elif codepoint < 0x7F:
            c_literals.append(character)
        elif codepoint <= 0xFFFF:
            c_literals.append(f"\\u{codepoint:04X}")
        else:
            c_literals.append(f"\\U{codepoint:08X}")

    return 'u"' + "".join(c_literals) + '"'


def generate_ui_strings_c_header() -> str:
    definitions = "\n".join(
        "#define SCRAN_UI_STRING_{} {}".format(
            name.upper(), as_c_utf16_string_literal(ui_string)
        )
        for name, ui_string in UI_STRINGS.items()
    )

    return dedent(
        """\
        // Generated from ui_strings_list.py by generate_font.py. Do not edit.
        #ifndef SCRAN_UI_STRINGS_H
        #define SCRAN_UI_STRINGS_H

        {definitions}

        #endif
        """
    ).format(definitions=definitions)


def generate_c_source(font_bytes: bytes, header_filename: str) -> str:
    byte_rows = []

    for offset in range(0, len(font_bytes), 16):
        row = ", ".join(f"0x{byte:02x}" for byte in font_bytes[offset : offset + 16])
        byte_rows.append(f"    {row},")

    array_contents = "\n".join(byte_rows)

    return dedent(
        """\
        // Generated by generate_font.py. Do not edit.
        #include "{header_filename}"

        _Alignas(16) const unsigned char scran_font_ttf[] = {{
        {array_contents}
        }};

        const size_t scran_font_ttf_size = sizeof(scran_font_ttf);
        """
    ).format(
        header_filename=header_filename,
        array_contents=array_contents,
    )


def write_if_changed(output_path: Path, contents: bytes) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if not output_path.exists() or output_path.read_bytes() != contents:
        output_path.write_bytes(contents)

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    # TODO: Maybe make this slightly easier to invoke manually.
    #       Its current shape is mostly for plugging into meson.
    parser.add_argument("--iosevka-font", type=Path, required=True)
    parser.add_argument("--nerd-font", type=Path, required=True)
    parser.add_argument("--outpath-ttf", type=Path, required=True)
    parser.add_argument("--outpath-c-source", type=Path, required=True)
    parser.add_argument("--outpath-c-header", type=Path, required=True)
    parser.add_argument("--outpath-ui-strings-header", type=Path, required=True)
    return parser.parse_args()

def main() -> None:
    args = parse_args()

    bytes_font, codepoint_count = generate_font(args.iosevka_font, args.nerd_font)
    bytes_c_header              = generate_c_header().encode("ascii")
    bytes_c_source              = generate_c_source(bytes_font, args.outpath_c_header.name).encode("ascii")
    bytes_ui_strings_header     = generate_ui_strings_c_header().encode("ascii")

    write_if_changed(args.outpath_ttf, bytes_font)
    write_if_changed(args.outpath_c_header, bytes_c_header)
    write_if_changed(args.outpath_c_source, bytes_c_source)
    write_if_changed(args.outpath_ui_strings_header, bytes_ui_strings_header)

    print(f"{args.outpath_ttf} ({codepoint_count} codepoints, {len(bytes_font)} bytes)")


if __name__ == "__main__":
    main()
