# HTML message display with litehtml: proposed fix

*Written 2026-11-20. Not implemented: a plan for deciding whether to add a
litehtml viewer to the current JX interface, to an FLTK port, or to both. It
complements `GUI-TOOLKIT-ANALYSIS.md` (section 7, option C, and section 9),
which is not changed by this document.*

## 1. Summary

Mulberry does not display HTML mail as HTML today. Its own parser flattens it
into styled text: no layout, no tables, no CSS, no images.

The proposal: a new **litehtml-based message viewer** for `text/html` parts,
drawing with Cairo, with images from the message itself, remote content
blocked by default, and Mulberry's existing link handling.

> **Preferred future path for text and drawing (added 2026-11-23): stack S1,
> Cairo + HarfBuzz without Pango and GLib** (section 10). Both libraries were
> built statically and tested on FreeBSD. On Linux, HarfBuzz may be used
> statically linked or from the system (a packager's choice; the default Linux
> build behaviour is still to be discussed). Sections 3–9 were written for
> litehtml's upstream Cairo+Pango container: where they say Pango, read "the
> text layer", which S1 implements with HarfBuzz and fontconfig.

- **In the current JX interface:** about 1.5–2 person-months. Most of the work
  (the viewer core and its Mulberry integration) is independent of the toolkit
  and is reused in an FLTK or Qt port.
- **In an FLTK port:** the same core with simpler glue (FLTK can hand out a
  Cairo context), but only as part of the port itself.

## 2. Current state (from the code)

### How a message body reaches the screen

1. `CMessageView` (`Linux/Sources/Application/Message/CMessageView.cpp`)
   chooses the part to show (`mCurrentPart`). For `multipart/alternative` it
   uses `CAttachment::FirstDisplayPart()`
   (`Sources_Common/Mail/Attachments/CAttachment.cpp`).
2. The part is decoded to Unicode (UTF-16, `mUTF16Text`), then passed to
   `CFormattedTextDisplay::ParseBody()`
   (`Linux/Sources/Formatting/CFormattedTextDisplay.cpp`, 1,116 lines).
3. For `text/html`, `CParserHTML` (`Sources_Common/Formatting/CParserHTML.cpp`,
   2,181 lines) converts the HTML into plain text plus **styled runs**
   (`CFormatList`: fonts, sizes, colours, bold/italic/underline, alignment,
   quoted blocks) and clickable elements (`CClickList`, `CAnchorElementMap`).
4. The result is shown in `CFormattedTextDisplay`, a JX 16-bit text editor
   (`CSpacebarEditView` → `CTextDisplay` → `JXTEBase16`) in read-only mode.

### What is lost

- **Layout:** tables, columns, widths, padding, borders, backgrounds of
  blocks. Newsletters and most commercial mail become a long column of text.
- **CSS:** `<style>` blocks and most inline styles.
- **Images:** `CParserHTML::HandleImage()` outputs only the `ALT` text. Images
  sent in the message (`multipart/related` with `cid:` references) are not
  shown inline.

### What exists and can be reused

- **View modes** (`EView` in `Sources_Common/Formatting/ETag.h`):
  formatted, plain, raw, "view as HTML / enriched / fixed font / raw".
- **Links:** `CClickElement`, URL launching (`CFormattedTextDisplay::LaunchURL`,
  `OnOpenLink`, `OnCopyLink`), `mailto:` handling.
- **Parts and Content-ID:** `CAttachment` tree, `CMIMEContent::GetContentId()`;
  reading a part's data goes through the existing attachment code (IMAP
  `FETCH BODY[…]`).
- **Quoting for reply and forward:** done on the text produced by the parsers;
  it can stay as it is.
- There is **no "remote images" preference** today, because images are never
  loaded.

## 3. litehtml

- An HTML/CSS layout engine: BSD-3 licence, about 44,000 lines, Gumbo HTML
  parser included. Very active (upstream commit `efb0ec8` on 2026-11-20).
- **No JavaScript**, which is what a mail viewer needs.
- It draws nothing itself: the application implements a `document_container`
  interface (fonts, text, images, borders, backgrounds, clipping, links,
  cursor, CSS import). Upstream ships ready **Cairo** and **Cairo+Pango**
  containers.
- CSS coverage: HTML + CSS 2.1, much of CSS 3 (flexbox, gradients, partial grid).
  That is enough for email, which is mostly table layouts with inline styles.

**Measured on the development machine** (details in `GUI-TOOLKIT-ANALYSIS.md`,
section 9), with the upstream Cairo+Pango container, 800×1000 viewport:

| | Small HTML mail (1.7 KB) | Large newsletter (83 KB, 120 table blocks) |
|---|---|---|
| Memory (total, including Pango/Cairo) | 25 MB | 47 MB |
| Time to parse, lay out and paint | 37 ms | 124 ms |
| Code added | 1.3 MB, statically linked | |

## 4. Design common to both toolkits

### 4.1 Where the viewer is used

- **New:** `text/html` parts in the **formatted** view mode.
- **Unchanged:** plain and enriched text, all "view as …" and raw modes, the
  composer, and the reply/forward quoting, which keeps using `CParserHTML`'s
  text output.
- A menu item or toolbar button switches a message between the new viewer and
  the current text rendering, which is also useful for copying text.

### 4.2 The container (Mulberry's `document_container`)

| Callback | Mulberry behaviour |
|---|---|
| fonts and text (`create_font`, `text_width`, `draw_text`) | upstream Cairo+Pango container: Pango gives fallback per character, emoji, right-to-left text and complex scripts. **Preferred future stack S1 (section 10):** HarfBuzz shapes, fontconfig (DirectWrite/CoreText on Windows/macOS) chooses fonts and fallback, Cairo draws the glyphs |
| `load_image`, `get_image_size`, `draw_image` | **`cid:`**: find the sibling part with that Content-ID in the enclosing `multipart/related`, fetch it with the existing attachment code, decode PNG/JPEG/GIF/WebP. **`data:`**: decode in place. **`http:`/`https:`**: **blocked by default**, with a bar "Remote content blocked – Load" per message; later a per-sender "always load" list |
| `import_css` | only `cid:`/`data:` style sheets; remote style sheets follow the remote-content rule |
| `on_anchor_click` | existing link handling: browser for `http(s):`, Mulberry's composer for `mailto:`, `#anchor` scrolls inside the message |
| `set_cursor`, `on_mouse_event` | hand cursor over links, status line shows the link target |
| `get_media_features`, `get_viewport` | viewer width, screen DPI; `prefers-color-scheme: light` |

### 4.3 Safety

- No JavaScript (litehtml has none); `<form>`, `<iframe>`, `<object>` and
  `<embed>` are ignored.
- **Remote content is blocked by default.** That also blocks tracking pixels
  and remote fonts.
- Size limits: maximum HTML size and image dimensions, to stop
  decompression or memory bombs.
- Untrusted input reaches a C++ parser in the Mulberry process:
  - vendor litehtml as a submodule and keep it updated;
  - optionally run its fuzz targets on mail corpora before release.

### 4.4 Features to write around litehtml

| Feature | How | Effort |
|---|---|---|
| Scrolling | widget height = document height; draw only the visible area (clip), as in the measurement | small |
| Text selection and copy | hit-test with litehtml's render tree (element under the mouse, text runs and their positions); collect the selected text; copy as plain text (HTML copy later) | ~2 weeks |
| Find in message | search the document's text runs; highlight matches; scroll to them | ~1 week |
| Printing | lay out again at page width into a Cairo PDF or PostScript surface; hand it to the existing print path (`lpr`) | ~1 week |
| Charset | Mulberry already decodes parts to Unicode; convert to UTF-8 for litehtml | trivial |
| Keyboard | page up/down, home/end, space-to-scroll as in the current view | small |

### 4.5 Building

- `Libraries/litehtml` as a git submodule (upstream repository), built as a
  static library (its `src/*.cpp` and `src/gumbo/*.c`, plus the Cairo and
  Cairo+Pango container sources). Either a small Makefile or a CMake step
  called from Mulberry's build.
- New build dependencies: `cairo`, `pango` (FreeBSD packages; present on any
  desktop); `cmake` if litehtml's own build is used.
- `scripts/build-freebsd.sh`: add them to the package check.
- **With the preferred stack S1 (section 10):** no Pango. Cairo (with pixman)
  and HarfBuzz as submodules built statically by a Meson step; new build-only
  tools `meson` and `ninja`. Run-time dependencies remain the system's FreeType,
  fontconfig and libpng. On Linux, HarfBuzz may come from the system instead
  (a build option, packager's choice).

## 5. Variant A: in the current JX interface

### Drawing

JX draws with Xlib and Xft and has no Cairo. Two ways to connect:

- **A1 (recommended): Cairo on the X drawable.**
  - In the viewer widget's `Draw()`, create a Cairo surface on the window's
    drawable (`cairo_xlib_surface_create(display, drawable, visual, w, h)`).
    With JX's double buffering this is the window's buffer pixmap
    (`JXWindow::itsBufferPixmap`), which Cairo can draw into like a window.
  - Translate to the widget's origin, clip to the update rectangle, and let
    litehtml draw through the **upstream Cairo+Pango container, unchanged**
    (for the prototype), or through the preferred **S1 container** (Cairo +
    HarfBuzz, section 10), which draws on the same Cairo surface.
  - JX change needed: `JXWindowPainter::GetDrawable()` exists but is
    `protected` in JX 2.5; a public accessor (one line, in the JX fork) is
    needed.
- **A2 (not recommended): a container drawing through `JXWindowPainter`**
  (rectangles, lines, Xft text, `JXImage`):
  - no anti-aliased shapes, rounded corners, transparency or gradients;
  - text through Xft with one font per string, so no fallback per character;
  - every drawing callback written by hand, instead of reusing upstream's
    container.

### Widget and events

- A new `CHTMLMessageView`, derived from `JXScrollableWidget`, in the message
  window and in the preview pane, in place of `CFormattedTextDisplay` while an
  HTML part is shown in formatted mode.
- Mouse and keyboard events go from JX to litehtml (`on_mouse_over`,
  `on_lbutton_down/up`) and to the selection logic. Cursor shapes come from
  `JXCursor`.
- The existing commands (copy, find, print, view modes) are routed to the new
  view when it is active.

### Consequences

- **Better text in mail than in the rest of the UI.** Pango in the viewer gives
  fallback per character, colour emoji, right-to-left text and complex scripts,
  while the rest of JX stays on Xft (see `FONT-FALLBACK-PLAN.md`). Two text
  stacks in one application is acceptable, but fonts may look slightly
  different between the viewer and the surrounding UI.
- **No HiDPI:** JX has none, so the viewer draws at 1:1 like the rest.
- **X11 only,** like JX.
- Memory: plus 25–47 MB while an HTML message is shown (measured including
  Pango/Cairo). That is in line with today's ~100 MB.

### Effort

| Part | Effort |
|---|---|
| Viewer core: container, images (`cid:`, `data:`), remote-content policy, links | ~3 weeks |
| Selection, copy, find | ~3 weeks |
| Printing | ~1 week |
| JX glue: widget, Cairo on the drawable, scrolling, events, commands | ~1–2 weeks |
| Build integration, packaging, tests | ~1 week |
| **Total** | **~1.5–2 person-months** |

## 6. Variant B: in an FLTK port

### Drawing

- FLTK built with Cairo and Pango (`FLTK_USE_PANGO=ON`,
  `FLTK_GRAPHICS_CAIRO=ON`, `FLTK_OPTION_CAIRO_EXT=ON`), as recommended in
  `GUI-TOOLKIT-ANALYSIS.md` section 7:
  - `Fl::cairo_make_current(window)` returns a `cairo_t` for the window being
    drawn;
  - on Wayland, FLTK itself draws everything with Cairo and Pango.
- The **same upstream Cairo+Pango container** is used. Viewer and UI share one
  text stack (Pango), so fonts, fallback and emoji are consistent everywhere.
- With the preferred **S1 container** (section 10), the viewer uses
  Cairo + HarfBuzz and needs no Pango. Whether FLTK itself is built with Pango
  (for fallback in its own UI text on X11) is then a separate decision for the
  port.

### Widget and events

- An `Fl_Widget` subclass inside a scroll container: `draw()` sets up Cairo
  (origin, clip, scale factor) and calls litehtml; `handle()` passes mouse and
  keyboard events to litehtml and to the selection logic.
- **HiDPI:** FLTK's per-screen scale factor is applied with `cairo_scale()`,
  so text and images are sharp.
- **X11 and Wayland** both work.

### Effort

- The viewer core, selection, find and printing are the same work as in
  variant A (about 7 weeks).
- The FLTK glue is simpler than JX's (no protected drawable, Cairo provided by
  FLTK): about 1 week.
- **But** it only exists once the FLTK port's message window exists, so it
  comes after a large part of the port (`GUI-TOOLKIT-ANALYSIS.md`: 6–12
  person-months in total).

## 7. Comparison

| | Variant A: current JX | Variant B: FLTK port |
|---|---|---|
| Available | after ~1.5–2 person-months | only with the port |
| Drawing | Cairo on the X drawable (needs a small JX change) | Cairo context from FLTK |
| Text in the viewer vs the rest of the UI | viewer (Pango, or HarfBuzz with S1) vs Xft: two stacks, the viewer is better | consistent if FLTK uses the same stack |
| Fallback per character, emoji, right-to-left in mail | ✓ (inside the viewer) | ✓ |
| HiDPI | ✗ | ✓ |
| Wayland | ✗ (X11 only) | ✓ |
| Glue effort | ~1–2 weeks | ~1 week |
| Reusable later | the core (~85% of the work) | — |

**Reuse when migrating:**
- The viewer core (container, `cid:`/`data:` images, remote-content policy,
  links, selection and find logic, printing through Cairo) does not depend on
  the toolkit if it is written against a thin interface: "give me a Cairo
  context and the visible rectangle; here are mouse and key events".
- **FLTK:** reused unchanged.
- **Qt:** the core logic is reused. For drawing, either keep Cairo (rendering
  into a `QImage` buffer that Qt then paints), or write a `QPainter`-based
  container (community containers exist). The latter makes Qt's own text
  stack replace Pango.

Unlike the font fallback in JX (`FONT-FALLBACK-PLAN.md`), most of this work is
therefore **not thrown away** by a later migration.

## 8. Suggested steps

1. **Prototype (1 week):**
   - litehtml as a submodule, built with Mulberry;
   - a read-only HTML view in the JX message window (variant A1), with no
     selection or images;
   - text through the S1 stack (section 10), or first through the upstream
     Cairo+Pango container and then S1 for comparison of memory, speed and
     text quality;
   - checked against real mail (newsletters, replies with quoted HTML, mail
     from common clients).
2. **Images and safety:** `cid:`/`data:` images, remote-content blocking with
   the "Load" bar, size limits.
3. **Selection, copy, find; then printing.**
4. **Preference:** "Display HTML messages: formatted (litehtml) / as text
   (current)", defaulting to formatted once stable.
5. Keep the core behind the thin drawing interface, so that an FLTK or Qt port
   reuses it.

## 9. Risks

| Risk | Mitigation |
|---|---|
| CSS gaps in litehtml for some mail | keep the "show as text" switch; report issues upstream; email HTML is conservative |
| Parser bugs on hostile input | keep the submodule updated; size limits; optional fuzzing |
| Cairo on JX's buffer pixmap: visual or colormap mismatch on unusual X setups | use the window's visual and depth from `JXDisplay`; fall back to rendering into a Cairo image surface and copying it with `XPutImage` |
| Selection on top of litehtml is the least ready-made part | budgeted separately (~2 weeks); copying plain text first, HTML copy later |

## 10. Text and drawing libraries: alternatives to Pango

*Analysed 2026-11-23. Measurements and builds on the development machine
(FreeBSD 15.1, amd64).*

> **Decision note: S1 (Cairo + HarfBuzz, without Pango and GLib) is the
> preferred future path** for the viewer's text and drawing. Cairo is built
> statically in-tree. **On Linux, HarfBuzz may be linked statically (bundled)
> or used from the system:** this is left to the package maintainer as a build
> option. The default Linux build behaviour is to be discussed later. S2
> (Blend2D) remains the lighter fallback option should Cairo become a problem.

### 10.1 Why replace Pango

- Pango depends on **GLib, GObject and GIO**: 3.7 MB of libraries. As packaged
  on FreeBSD, GLib alone brings **344 MiB** of dependencies (11 packages), and
  Pango's whole dependency set is 402 MiB (32 packages).
- Pango and GLib must be shipped on Windows and macOS as well (GIMP and Inkscape
  do it, but it is heavy).

### 10.2 What litehtml actually needs from the text layer

Verified in litehtml's source (commit `28a806e`):
- **litehtml breaks lines itself:** it splits text into words at spaces, and
  each CJK ideograph as its own word, in `document_container::split_text()`,
  which a container may override.
- **litehtml has no bidirectional-text support at all.**

So a container only needs, **per word**:
1. **fonts:** a font for the CSS family/weight/style, and a **fallback** font for
   characters it lacks (emoji, CJK, …);
2. **shaping:** characters → glyphs and positions (ligatures, kerning; real
   shaping for Arabic and Indic scripts);
3. **measuring and drawing** the glyphs;
4. **2D drawing:** rectangles, rounded borders, gradients, clipping, images;
5. **image decoding:** PNG, JPEG, GIF (WebP occasionally).

Pango's main features (paragraph layout, line breaking, bidi reordering) are
**not used** by a litehtml container.

### 10.3 Building blocks (measured)

| Library | Library size | Needs GLib? | Licence | Notes |
|---|---|---|---|---|
| GLib + GObject + GIO | 3.7 MB | — | LGPL | package 106 MiB; 344 MiB with its dependencies |
| Pango (+cairo, +ft2) | 0.6 MB | **yes** (links GLib, GObject, GIO) | LGPL | |
| Cairo + pixman | 2.1 MB | no; only the optional `cairo-gobject` part | LGPL-2.1 or MPL-1.1 / MIT | the FreeBSD package still depends on GLib |
| HarfBuzz | 1.2 MB | **optional**; the FreeBSD package is built with it | MIT | can also load fonts, give glyph outlines (`hb-draw`) and colour-glyph paint operations (`hb-paint`) |
| FreeType + fontconfig | 1.1 MB | no | FTL / MIT | 14 MiB with dependencies; on every Unix desktop |
| FriBiDi / libraqm / libunibreak | 0.12 / 0.03 / 0.14 MB | no | LGPL / MIT / zlib | bidi; itemisation + bidi + shaping; line breaking (UAX #14) |
| Blend2D | package 2.6 MiB, no dependencies | no | zlib | FreeBSD package from a git snapshot (`g20260703`) |
| plutovg | ~12,000 lines | no | MIT | Cairo-like; bundles FreeType's rasteriser code, `stb_truetype`, `stb_image` |
| Skia | 465 MiB dependency set (35 packages, incl. ICU) | no | BSD | heavy |

Blend2D, verified in its source (commit `58ca946`):
- own OpenType loader (TrueType/CFF outlines, `cmap`, `kern`, GSUB/GPOS
  lookups), so text can be drawn without FreeType; JIT-compiled pipelines
  (x86-64, AArch64) with a portable non-JIT option; built-in PNG, JPEG, BMP and
  QOI decoders;
- **missing:** script-specific shaping (Arabic joining, Indic reordering),
  colour glyphs (`COLR`/`CPAL` only appear as tag names), hinting and LCD
  subpixel anti-aliasing, GIF/WebP decoders; pre-1.0, very small team.

### 10.4 Candidate stacks

**S0. Cairo + Pango (litehtml's upstream container).**
- ➕ ready-made; fallback, colour emoji (via FreeType) and hinted text work out
  of the box.
- ➖ GLib; Pango and GLib to ship on Windows/macOS.

**S1. Cairo + HarfBuzz (without GLib) + FreeType + fontconfig: drop Pango, keep
Cairo. Preferred future path.**
- ➕ small change: Cairo still draws everything; text becomes HarfBuzz shaping +
  `cairo_show_glyphs` with FreeType fonts.
- ➕ keeps hinting and colour emoji (FreeType); no GLib; about 3.75 MB of
  statically linked code (measured, section 10.6).
- ➖ font fallback must be written: on Unix with fontconfig (`FcFontSort` with
  the characters of a run; about 30 lines in the test); DirectWrite /
  CoreText on Windows / macOS.
- ➖ Cairo is a slow-moving project; its FreeBSD package declares GLib (avoided
  by the static in-tree build).
- 🖥 Windows/macOS: Cairo has native Win32 and Quartz back ends; HarfBuzz and
  FreeType build everywhere.
- **Effort:** about +1 week over S0.

**S2. Blend2D + HarfBuzz (without GLib) + platform font discovery.**
- HarfBuzz shapes; Blend2D draws glyph runs (its own font engine), shapes,
  gradients and images (its PNG/JPEG decoders); colour emoji by replaying
  HarfBuzz's `hb-paint` operations on Blend2D; fonts and fallback through
  fontconfig / DirectWrite (`IDWriteFontFallback`) / CoreText
  (`CTFontCreateForString`).
- ➕ no GLib, Cairo or Pango: about 3.8 MB (Blend2D + HarfBuzz) plus
  fontconfig/FreeType; permissive licences only; very fast; identical drawing
  code on all platforms.
- ➖ no hinting (softer small text on low-DPI screens); colour-emoji glue
  (~1 week); GIF/WebP decoders needed; Blend2D pre-1.0 with a tiny team; JIT on
  macOS needs the hardened-runtime JIT entitlement or the slower non-JIT build.
- Variant **S2b:** FreeType rasterises glyphs (hinted, colour emoji), Blend2D
  composites them; better small text, more code (glyph cache).
- **Effort:** about +2–3 weeks over S0, plus about a week per platform for font
  discovery.

**S3. Blend2D alone (its own text engine).**
- ➕ lightest: one library, no dependencies.
- ➖ no complex-script shaping, no fallback, no colour emoji: fine for Latin,
  Cyrillic, Greek and CJK, not for international mail.

**S4. plutovg + HarfBuzz (HarfBuzz outlines → plutovg paths).**
- ➕ tiny (12k lines, MIT); Cairo-like API; builds everywhere.
- ➖ slower (no SIMD/JIT); glyph cache to write; small user base; no hinting.
- **Effort:** about +3–4 weeks.

**S5. Text helpers, combinable with S1/S2.**
- **libraqm** (MIT): bidi + itemisation + shaping over HarfBuzz +
  FriBiDi/SheenBidi; no font fallback; little value while litehtml has no
  line-level bidi.
- **libunibreak** (zlib): UAX #14 line-break opportunities in an overridden
  `split_text()` (CJK, punctuation); Thai still needs a dictionary (libthai).
- **FriBiDi / SheenBidi:** only useful if bidi is added to litehtml itself.

**S6. kb_text_shape (zlib, single C header) instead of HarfBuzz.**
- ➕ segmentation (direction, lines, words, graphemes), OpenType shaping
  including complex scripts, font-coverage checks, in one header. HarfBuzz's own
  tree includes a `kb_text_shape` back end.
- ➖ young (2025), single author, no published comparison with HarfBuzz; no
  rasteriser or font discovery. Worth re-evaluating later.

**S7. Native per platform** (DirectWrite + Direct2D on Windows, CoreText +
CoreGraphics on macOS, S1 or S2 on Unix).
- ➕ best text on each system with zero extra dependencies there.
- ➖ three containers to maintain; rendering differs per platform; about
  +3 weeks per extra platform.

**S8. Skia (with SkShaper / SkParagraph).**
- ➕ excellent, identical rendering everywhere.
- ➖ heavy (465 MiB dependency set as packaged, ICU, own build system).
  Rejected.

**S9. Provided by the GUI toolkit.**
- **Qt:** a `QPainter`-based container uses Qt's own HarfBuzz, fallback and
  drawing, with no extra libraries and no GLib (optional in Qt).
- **FLTK:** drawing through `fl_draw` is the lightest, but FLTK on X11 has no
  per-character fallback unless built with Pango; S1 fits FLTK better.

### 10.5 Platform summary

| Stack | FreeBSD/Linux | Windows | macOS | Fallback / colour emoji | Small-text quality |
|---|---|---|---|---|---|
| S0 Cairo+Pango | ✓ packages | ship GLib+Pango | ship GLib+Pango | ✓ / ✓ | ✓ hinted |
| **S1 Cairo+HarfBuzz** | ✓ (static build verified) | ✓ (Cairo Win32) + DirectWrite discovery | ✓ (Cairo Quartz) + CoreText discovery | to write / ✓ | ✓ hinted |
| S2 Blend2D+HarfBuzz | ✓ (packaged) | ✓ + DirectWrite discovery | ✓ + CoreText discovery (JIT entitlement) | to write / glue (~1 week) | ~ unhinted (S2b: ✓) |
| S4 plutovg+HarfBuzz | ✓ | ✓ | ✓ | to write / to write | ~ unhinted |
| S7 native | S1/S2 | ✓ DirectWrite/Direct2D | ✓ CoreText | ✓ / ✓ | ✓ native |
| S9 Qt | ✓ | ✓ | ✓ | ✓ / ✓ | ✓ |

### 10.6 Verified: static Cairo and HarfBuzz builds (S1)

**Cairo 1.18.6 + pixman 0.46.4, static:**
- Sources from freedesktop.org's Git; pixman placed in Cairo's tree as a Meson
  subproject (directory name expected by Cairo's wrap file:
  `subprojects/pixman-0.44.2`).
- Configuration:
  ```sh
  CC=gcc14 meson setup build --buildtype=release --default-library=static \
      --wrap-mode=nodownload --force-fallback-for=pixman \
      -Dglib=disabled -Dxlib=enabled -Dxcb=disabled -Dfreetype=enabled \
      -Dfontconfig=enabled -Dpng=enabled -Dzlib=enabled -Dtests=disabled \
      -Dspectre=disabled -Dlzo=disabled -Dsymbol-lookup=disabled -Dtee=disabled \
      -Dgtk2-utils=disabled -Ddwrite=disabled -Dquartz=disabled \
      -Dpixman:tests=disabled -Dpixman:demos=disabled -Dpixman:gtk=disabled \
      -Dpixman:libpng=disabled -Dpixman:openmp=disabled
  ```
- Enabled: Image, Xlib/XRender (drawing on JX's X windows), PDF, PostScript and
  SVG surfaces (printing); FreeType and fontconfig fonts.
- Build: **39 s**; `libcairo.a` 2.5 MB, `libpixman-1.a` 1.4 MB; `libcairo.a`
  references no GLib symbols.

**HarfBuzz 15.2.0, static:**
- Configuration:
  ```sh
  CC=gcc14 CXX=g++14 meson setup build --buildtype=release \
      --default-library=static --wrap-mode=nodownload -Dfreetype=enabled \
      -Dglib=disabled -Dgobject=disabled -Dcairo=disabled -Dchafa=disabled \
      -Dpng=disabled -Dzlib=disabled -Dicu=disabled -Dgraphite=disabled \
      -Dgraphite2=disabled -Dfontations=disabled -Dgdi=disabled \
      -Ddirectwrite=disabled -Dcoretext=disabled -Dharfrust=disabled \
      -Dkbts=disabled -Dwasm=disabled -Draster=disabled -Dvector=disabled \
      -Dgpu=disabled -Dgpu_demo=disabled -Dsubset=disabled -Dtests=disabled \
      -Dintrospection=disabled -Ddocs=disabled -Ddoc_tests=false \
      -Dutilities=disabled -Dbenchmark=disabled
  ```
- Uses HarfBuzz's **built-in Unicode data**: no GLib, no ICU.
- Build: **93 s**; `libharfbuzz.a` 3.6 MB; references **no GLib and not even the
  C++ standard library**.

**Test of the complete S1 chain** (fontconfig fallback by coverage → HarfBuzz
shaping → Cairo `cairo_show_glyphs`), with HarfBuzz, Cairo and pixman linked
statically:

| Run | Font chosen | Result |
|---|---|---|
| `Office efficiency: fi ffl` | Noto Sans | 25 characters → 18 glyphs: ligatures `ffi`, `fi`, `ffl` |
| `Привет, éçà` | Noto Sans | correct |
| `مرحبا بالعالم` (Arabic) | DejaVu Sans, chosen automatically (Noto Sans has no Arabic) | shaped right to left, letters correctly joined |

- Libraries loaded at run time: fontconfig, FreeType, libpng, zlib, the X11
  libraries and libc. **No HarfBuzz, Cairo, pixman, GLib or libstdc++.**
- Size (stripped): Cairo + pixman ≈ 2.1 MB, HarfBuzz ≈ 1.65 MB: **≈ 3.75 MB**
  added to Mulberry.
- Not tested yet: colour emoji (Cairo's FreeType backend supports them).

### 10.7 Integration and packaging

- **Submodules** (ideally own forks, like the others): `Libraries/cairo` (with
  pixman under its `subprojects/`) and `Libraries/harfbuzz`, built by a Meson
  step called from `configure`/`Makefile` before the rest, as JX is today.
- **Build cost:** about 2 extra minutes on 8 cores; new build-only tools
  `meson` and `ninja` (packaged; Meson needs Python). No new run-time
  dependencies.
- **Build options for packagers** (default Linux behaviour to be discussed):

  | Library | Bundled, static | From the system, shared |
  |---|---|---|
  | HarfBuzz | ✓ (verified) | ✓ on Linux, at the packager's choice |
  | Cairo + pixman | ✓ (verified); needed on FreeBSD to avoid the package's GLib dependency | possible on Linux distributions where `cairo-gobject` is a separate package |

- **Licences:** HarfBuzz MIT-style; Cairo under its MPL-1.1 option (static
  linking only requires providing the source of Cairo's own files and changes);
  pixman MIT. All compatible with distributing Mulberry under Apache-2.0.
- **Caveats:**
  - **Security updates:** with static linking, fixes reach Mulberry only when
    the submodules are updated (pixman has had occasional overflow CVEs).
  - **Linux:** some distributions build the system FreeType *with* HarfBuzz;
    a bundled static HarfBuzz then coexists with the system one in the process.
    It works, but versions must stay compatible; a reason to offer the system
    HarfBuzz option there. FreeBSD's FreeType does not link HarfBuzz.
  - FreeType, fontconfig and libpng stay shared system libraries (fontconfig
    must match the system's font configuration anyway).

### 10.8 Sources (section 10)

- [HarfBuzz meson options](https://flutter.googlesource.com/third_party/harfbuzz/+/HEAD/meson_options.txt), [HarfBuzz BUILD.md](https://flutter.googlesource.com/third_party/harfbuzz/+show/refs/heads/main/BUILD.md), [HarfBuzz (GitHub)](https://github.com/harfbuzz/harfbuzz)
- [libraqm README](https://github.com/HOST-Oman/libraqm/blob/master/README.md), [Raqm on LinuxLinks](https://www.linuxlinks.com/raqm-font-library/)
- [kb single-header libraries](https://hn.jakechampion.name/reader/https://github.com/JimmyLefevre/kb), [HarfBuzz `kb_text_shape` back end](https://flutter.googlesource.com/third_party/harfbuzz/+/HEAD/src/hb-kbts.cc)
- [DirectWrite colour fonts](https://learn.microsoft.com/windows/win32/directwrite/color-fonts)
- Verified locally: litehtml (commit `28a806e`), Blend2D (commit `58ca946`),
  plutovg (commit `557a818`), Cairo 1.18.6 and pixman 0.46.4 (static build),
  HarfBuzz 15.2.0 (static build), installed HarfBuzz/Pango/Cairo libraries and
  headers, FreeBSD package database.
