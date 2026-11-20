# Font fallback per character in JX: proposed fix

*Written 2026-11-20. Not implemented: a plan for deciding between this fix and
a GUI toolkit migration (see `GUI-TOOLKIT-ANALYSIS.md`).*

## 1. Problem

JX draws text with Xft using **one font per string**. A character the font has
no glyph for is drawn as an empty box. Xft itself has no fallback per
character.

Mulberry works around this with text substitution:
- `FilterUTF8ForDisplay()` in `Linux/Sources/Application/General/CMulberryCommon.cpp`:
  subjects, lists, headers, calendar summaries;
- `CConverterBase::ToUTF16()` in `Sources_Common/i18n/Charsets/Converters/CConverterBase.cpp`:
  message bodies and other text views.

Since the Unicode fixes of 2026-11-20, these filters let letters of every script
(up to U+FFFF) through, and substitute only:
- punctuation and symbols U+2000–U+2BFF: curly quotes and dashes become ASCII,
  arrows and dingbats become `[U+XXXX]` or a name;
- emoji and other characters beyond U+FFFF: `[:grinning face:]`, `[:)]`;
- invisible formatting characters.

The rule is `IsDirectlyDisplayable()` in `Sources_Common/i18n/Charsets/CUnicodeFilter.h`.

Remaining visible issues:
- symbols such as → ✓ ❤ ∀ ─ ☀ and typographic quotes are shown as substitutes,
  although installed fonts have them;
- letters of a script the current font lacks (e.g. CJK with a Latin-only font)
  are now drawn as boxes;
- emoji are shown as names.

## 2. How upstream JX does it

Upstream JX (github.com/jafl/jx_application_framework, checked at commit
`ae78335`, 2026-08-22):
- `JXFontManager::HasGlyphForCharacter()` checks a character with fontconfig
  (`FcCharSetHasChar(info.xftt->charset, c)`);
- `JXFontManager::GetSubstituteFontName()` walks a **hard-coded list**,
  `kFallbackFontNames`, and returns the first font that has the glyph:
  - Linux: "Source Han Sans CN Normal", "Noto Sans CJK SC", then the default font;
  - macOS: "Mshtakan", "Arial Unicode MS", then the default font;
- `JFont::SubstituteToDisplayGlyph()` switches the font, and is called while
  drawing (`JPainter`, `JXWindowPainter`), in menus (`JXTextMenuTable`) and in
  styled text (`JXStyledText::AdjustFontToDisplayGlyphs`).

A fixed list of font names does not suit FreeBSD or arbitrary desktops: those
exact fonts are often not installed.

## 3. Proposed design for Mulberry's JX 2.5

### Choosing the fallback font

- For a character the current font lacks (`XftCharExists` /
  `FcCharSetHasChar`), ask **fontconfig** for the best font that has it:
  `FcFontSort` (or `FcFontMatch`) with a pattern of the current family, size,
  weight and slant, plus an `FcCharSet` containing the character.
- Cache the result per (font, Unicode block), so the lookup happens once per
  block and the drawing cost stays negligible.
- **No configuration needed by default:** fontconfig follows the system's font
  configuration. To steer the choice (e.g. a preferred emoji font), the user
  edits fontconfig's own file, `~/.config/fontconfig/fonts.conf` (a `<prefer>`
  alias), which applies to every application.
- Optional later: a Mulberry preference listing preferred fallback fonts,
  tried before fontconfig (upstream's approach, as an override).

### Where the code changes (all in `Libraries/JX`, the fork)

| Place | Change |
|---|---|
| `libjx/code/JXFontManager.cpp/.h` | glyph check; fallback lookup through fontconfig; cache of opened fallback `XftFont`s |
| `libjx/code/JXGC.cpp`: `DrawString` (UTF-8) and `DrawString16` (UTF-16) | split the string into runs of characters drawable with the same font; draw each run with its font at the advancing x position |
| `JXFontManager` width functions (`GetStringWidth`, `GetCharWidth`, `GetCharWidth16`, `GetStringWidth16`) | apply the same fallback: widths must match the drawing, or caret, selection and line wrapping in the editors are misplaced |
| line metrics | scale fallback glyphs to the main font's ascent and descent (a tall emoji glyph would otherwise be clipped or change the line height) |

Mulberry itself then needs only one change: reduce the substitution in
`IsDirectlyDisplayable()` to characters **no installed font** can draw. That
needs a query to JX ("is there any font with this glyph?"), or keeping the
tables only for characters beyond U+FFFF in the text views (see section 5).

## 4. What would be fixed

Coverage of the fonts installed on the development machine (`fc-list
:charset=…`):

| Characters | Example | Installed fonts having them | Lists, subjects, headers, calendar (UTF-8 drawing) | Message bodies and editors (UTF-16) |
|---|---|---|---|---|
| Letters missing from the main font (e.g. CJK) | 中 | 17 (Droid Sans Fallback, IBM Plex Sans JP, …) | ✓ | ✓ |
| Arrows | → | 186 | ✓ | ✓ |
| Check marks, dingbats | ✓ | 80 (DejaVu Sans, …) | ✓ | ✓ |
| Heart and other symbols | ❤ ☀ | 8 (DejaVu Sans, …) | ✓ | ✓ |
| Mathematical operators | ∀ | 85 | ✓ | ✓ |
| Box drawing | ─ | 94 | ✓ | ✓ |
| Typographic quotes and dashes | “ ” – … | most text fonts | ✓ (no longer replaced by ASCII) | ✓ |
| Emoji | 😀 👍 | DejaVu Sans (part), Noto Sans Symbols 2, Twitter Color Emoji | ✓ | ✗ (section 5) |

**Colour emoji:** libXft 2.3.9 (installed) can draw colour bitmap fonts.
- `noto-emoji` (Noto Color Emoji) would give colour emoji.
- The installed Twitter Color Emoji keeps its colour as SVG, which Xft cannot
  draw, so its monochrome glyphs would be used.

Either is better than `[:name:]`.

Related FreeBSD packages: `noto-emoji`, `noto-sans-symbols`, `noto-basic`,
`noto-extra`, `twemoji-color-font-ttf`.

## 5. Limits

- **Emoji in message bodies and editors.** The text views use JX's 16-bit text
  layer (`JTextEditor16`, `JString16`), which holds characters up to U+FFFF only.
  Emoji (U+1F000+) cannot be stored there, so the text views keep the
  `[:name:]` substitution. Lifting this needs UTF-16 surrogate pairs throughout
  `JTextEditor16`: caret movement, selection, word boundaries, drawing. That is a
  separate and larger change.
- **No text shaping:** Xft has no HarfBuzz, so right-to-left text (Arabic,
  Hebrew) is not reordered and complex scripts (Indic, Thai with marks) are not
  shaped.
- **Combining accents** (U+0300–U+036F) are still dropped by
  `IsInvisibleUnicode()`. With fallback in place they could be drawn
  (overstruck), but proper positioning needs shaping.
- **Fonts chosen in preferences by XLFD name** (core X font style names) are
  opened through Xft (`XftFontOpenXlfd`), so the fallback applies to them too,
  but metrics may differ more between the main and fallback fonts.

## 6. Effort and risk

- About **1 week** for the JX part. Most of the work is keeping widths,
  caret and selection consistent in the editors, and testing with mixed text
  (Latin + symbols + CJK, various sizes and styles).
- Risk: low to medium. The change is confined to JX's font manager and drawing
  context. Mulberry's code changes only in the substitution rule.
- It must be committed in the JX fork first, then the submodule pointer in the
  main repository (as for the `JString16` fix).

## 7. Comparison with a toolkit migration

| | Fallback in JX 2.5 (this plan) | Qt 6 | FLTK 1.4 built with Pango |
|---|---|---|---|
| Fallback per character | to implement (about 1 week) | built in (`QFont` + fontconfig) | built in (Pango) |
| Emoji everywhere, including message bodies | ✗ (16-bit text layer) | ✓ (UTF-16 with surrogates, colour emoji) | ✓ (UTF-8) |
| Right-to-left, complex scripts | ✗ | ✓ (HarfBuzz) | ✓ (HarfBuzz via Pango) |
| Effort | ~1 week | part of the port (8–15 person-months) | part of the port (6–12 person-months) |

**Summary:**
- The JX fix is a cheap, contained improvement for the current UI: symbols and
  all scripts with installed fonts, and emoji outside message bodies.
- It is throw-away work if the GUI is migrated, since both Qt and FLTK with
  Pango provide fallback, emoji and shaping themselves.
- It is worth doing only if the JX UI will remain in use for a long time, for
  example as the working version during an incremental port.
