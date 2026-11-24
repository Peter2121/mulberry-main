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

> **Implementation started (2026-11-23), branch `html-viewer`:** variant A
> (current JX interface) with stack S1. The submodules are in place (section
> 10.7); the work is split into small stages and tasks in **section 11**, each
> leaving the build and the running program correct. Stages 1 and 2 are done:
> the libraries are built with Mulberry, and the viewer core with its test
> tool `html2png` is in `Libraries/HTMLView`.

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
  The submodule follows **master** (commit `28a806e`, 2026-11-22), not the
  last release v0.10 (2026-06-14): in v0.10, the layout time of nested
  tables and flex boxes grows about 3× per nesting level (12 nested tables:
  1.4 s; 14: 22 s), which a newsletter or a hostile mail can reach; master
  lays out both in under a millisecond (measured 2026-11-23 with `html2png`).
  Master's interface has the same functions as v0.10's, but `pixel_t` became
  a class and the `litehtml::string` alias is gone (an earlier note here
  called the interfaces identical: the comparison had normalised the type
  names); the viewer's code compiles with both.
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
  called from Mulberry's build. With S1, only the toolkit-neutral Cairo part
  of the upstream container is used (`containers/cairo/container_cairo.cpp`,
  `cairo_borders.cpp`, `conic_gradient.cpp`: borders, backgrounds, gradients,
  images; no Pango or GLib); fonts and text are Mulberry's own S1 code
  (section 11, stage 2).
- New build dependencies: `cairo`, `pango` (FreeBSD packages; present on any
  desktop); `cmake` if litehtml's own build is used.
- `scripts/build-freebsd.sh`: add them to the package check.
- **With the preferred stack S1 (section 10):** no Pango. pixman, Cairo and
  HarfBuzz as separate submodules built statically by a Meson step (pixman
  first, section 10.6); new build-only tools `meson` and `ninja`. Run-time dependencies remain the system's FreeType,
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

*Superseded by the detailed stages in section 11; kept as the overview.*

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
- **Layout used from now on (verified 2026-11-23): pixman as a separate
  submodule**, built first and installed into a staging prefix; Cairo is then
  configured against it through pkg-config. Both from the submodules
  `Libraries/pixman` and `Libraries/cairo`, with build directories outside the
  source trees (the submodules stay clean):
  ```sh
  DIST=$PWD/Libraries/htmldeps/dist
  CC=gcc14 meson setup Libraries/htmldeps/build/pixman Libraries/pixman \
      --prefix=$DIST --libdir=lib --buildtype=release \
      --default-library=static --wrap-mode=nodownload \
      -Dtests=disabled -Ddemos=disabled -Dgtk=disabled -Dlibpng=disabled \
      -Dopenmp=disabled
  ninja -C Libraries/htmldeps/build/pixman install

  PKG_CONFIG_PATH=$DIST/libdata/pkgconfig:$DIST/lib/pkgconfig \
  CC=gcc14 meson setup Libraries/htmldeps/build/cairo Libraries/cairo \
      --prefix=$DIST --libdir=lib --buildtype=release \
      --default-library=static --wrap-mode=nodownload \
      -Dglib=disabled -Dxlib=enabled -Dxcb=disabled -Dfreetype=enabled \
      -Dfontconfig=enabled -Dpng=enabled -Dzlib=enabled -Dtests=disabled \
      -Dspectre=disabled -Dlzo=disabled -Dsymbol-lookup=disabled -Dtee=disabled \
      -Dgtk2-utils=disabled -Ddwrite=disabled -Dquartz=disabled
  ninja -C Libraries/htmldeps/build/cairo install
  ```
  - **Pitfall found:** on FreeBSD, Meson installs `.pc` files into
    `libdata/pkgconfig`, on Linux into `lib/pkgconfig`; `PKG_CONFIG_PATH` must
    name both. With only `lib/pkgconfig`, Cairo silently used the **system**
    pixman (also 0.46.4, `/usr/local/include/pixman-1`). Check:
    `grep -- '-I.*pixman-1' build.ninja` must show the staging directory.
  - Build time: pixman 14 s, Cairo 21 s. A test program linked with
    `libcairo.a` and `libpixman-1.a` by full path loads no shared Cairo or
    pixman.
- First verification (same versions, kept for reference): pixman placed in
  Cairo's tree as a Meson subproject (directory name expected by Cairo's wrap
  file: `subprojects/pixman-0.44.2`), configured with
  `--force-fallback-for=pixman` and `-Dpixman:…` options instead of the
  separate pixman step; the other options as above.
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

- **Submodules** (added 2026-11-23 on branch `html-viewer`, from the upstream
  repositories for now; own forks later, like the other libraries, when
  patches are needed), each pinned to a release tag:

  | Submodule | Upstream | Tag |
  |---|---|---|
  | `Libraries/litehtml` | github.com/litehtml/litehtml | master `28a806e` (section 3: v0.10's nested layout is exponential) |
  | `Libraries/pixman` | gitlab.freedesktop.org/pixman/pixman | `pixman-0.46.4` |
  | `Libraries/cairo` | gitlab.freedesktop.org/cairo/cairo | `1.18.6` |
  | `Libraries/harfbuzz` | github.com/harfbuzz/harfbuzz | `15.2.0` |

- **pixman is a separate submodule**, not a Meson subproject inside Cairo's
  tree (which would mean changing Cairo's tree). It is built first and
  installed into a staging directory; Cairo then finds it through
  `PKG_CONFIG_PATH` (section 10.6). All four are built by a step in
  `Libraries/Makefile` before the rest, as JX is today; build and staging
  directories stay outside the submodules, which remain clean.
- **Link the static archives by full path** (`…/dist/lib/libcairo.a
  …/dist/lib/libpixman-1.a`), not with `-lcairo -lpixman-1`: the system has
  shared `libcairo.so` and `libpixman-1.so` in `/usr/local/lib`, and a `-l`
  could pick them.
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

## 11. Implementation stages

*Added 2026-11-23. Branch `html-viewer`; variant A (current JX interface) with
stack S1 (Cairo + HarfBuzz). Replaces the overview in section 8.*

### 11.1 Rules for every task

- **One task, one commit.** Each task is reviewed and committed before the next
  starts.
- **After every task the build and the program stay correct:**
  - full build from a clean tree, with `scripts/build-freebsd.sh` and with the
    plain `./configure && gmake` path used on Linux;
  - Mulberry starts, and mail displays exactly as before, unless changing that
    is the point of the task.
- **The new viewer stays switched off until stage 8:**
  - at build time, `configure --disable-html-viewer` builds Mulberry without
    any of the new libraries. This path is kept working throughout, also as a
    fallback for packagers;
  - at run time, a preference (default off) selects the new viewer.
- **The viewer core knows nothing about JX or Mulberry.** It lives in
  `Libraries/HTMLView` and is compiled without Mulberry's prefix header.
  Mulberry gives it UTF-8 HTML, a Cairo context and the visible rectangle,
  and answers its callbacks (image data, link clicked, cursor). The same core
  then serves an FLTK port (section 7).
- **Generated files stay out of the submodules:** build and staging
  directories go under `Libraries/htmldeps/` (git-ignored); the four
  submodules stay clean.
- **Checks:** each task lists its own. Visual checks are done by the developer,
  on PNG files from the test tool or in the running Mulberry.
- **Sizes:** S = up to half a day, M = 1–2 days, L = 3–5 days.

### 11.2 Stage 0: submodules (done)

| # | Task | Check |
|---|---|---|
| 0.1 | Branch `html-viewer`; submodules `Libraries/litehtml` (v0.10, then master `28a806e` during stage 2, section 3), `Libraries/pixman` (pixman-0.46.4), `Libraries/cairo` (1.18.6), `Libraries/harfbuzz` (15.2.0), from the upstream repositories | ✓ litehtml v0.10 built; pixman → Cairo built with the separate-pixman layout (section 10.6) |

### 11.3 Stage 1: building the libraries (Mulberry unchanged)

| # | Task | Check | Size |
|---|---|---|---|
| 1.1 | `.gitignore`: `Libraries/htmldeps/`. `scripts/build-freebsd.sh`: `meson`, `ninja` and `cmake` (see 1.5) added to the requirements, and the four submodules to `check_submodules` | the script builds as before; a missing tool is reported with its `pkg install` line | S |
| 1.2 | `Libraries/Makefile.in`: target `htmldeps-pixman` (Meson, static, installed into `Libraries/htmldeps/dist`); not yet part of `all` | `gmake -C Libraries htmldeps-pixman` gives `dist/lib/libpixman-1.a`; the normal build is unchanged | S |
| 1.3 | Target `htmldeps-cairo`, after pixman, with `PKG_CONFIG_PATH` on both staging `pkgconfig` directories (section 10.6) | `libcairo.a`; `build.ninja` shows the staged pixman headers; no GLib symbols | S |
| 1.4 | Target `htmldeps-harfbuzz` (options from section 10.6) | `libharfbuzz.a`; no GLib or libstdc++ symbols | S |
| 1.5 | Target `htmldeps-litehtml` (litehtml's own CMake, out of tree, static, tests off) | `liblitehtml.a` and `libgumbo.a` in `dist/lib`, headers in `dist/include` | S |
| 1.6 | `htmldeps` (the four targets) becomes part of `all`, with `clean` and `distclean`, and a `SKIPHTMLDEPS` switch like `SKIPJX`. Meson and CMake configure only once; later runs are incremental | fresh-clone build works; a second `gmake` finishes quickly; Mulberry unchanged | M |
| 1.7 | `configure.in`: `--enable-html-viewer` (default) / `--disable-html-viewer`. Enabled: defines `HTML_VIEWER` and substitutes include and link flags (static archives by full path, then the system FreeType, fontconfig, libpng, zlib and Xrender). Disabled: `htmldeps` skipped | both configurations build and run; the binary is the same size as before (nothing references the libraries yet) | M |

**Stage 1 done (2026-11-23).** Notes:
- `scripts/build-freebsd.sh -n` builds without the viewer (passes
  `--disable-html-viewer`; meson, ninja and cmake are then not required).
- The flags substituted by configure (`HTMLVIEW_CPPFLAGS`, `HTMLVIEW_LIBS`)
  are not used by `Linux/Makefile` yet: Mulberry links them from stage 3 on,
  so its binary is unchanged until then.
- Meson and CMake get only the compiler's name: configure's `CC` may carry a
  standard option (`gcc14 -std=gnu23`), and the libraries choose their own.
- **HarfBuzz and ragel:** when `ragel` is installed (it is on this machine),
  HarfBuzz's build regenerates its state machines into its source tree
  (only `#line` numbers differ), leaving the submodule modified. A stub
  `ragel` reporting version 0.0, given through a Meson native file, makes
  HarfBuzz ignore it.
- Build time from scratch: about 3 minutes on 8 cores (litehtml, pixman,
  Cairo and HarfBuzz in parallel); a rebuild with nothing changed: 6 s.

### 11.4 Stage 2: the viewer core, tested outside Mulberry

| # | Task | Check | Size |
|---|---|---|---|
| 2.1 | `Libraries/HTMLView` skeleton:<br>• own Makefile, static `libHTMLView.a`, built from `Libraries/Makefile` only when enabled;<br>• public header with no litehtml types: load HTML (UTF-8) at a width, document height, draw into a `cairo_t` with a clip rectangle, mouse events in, callbacks out;<br>• container derived from upstream's `container_cairo`, with stub text functions (fixed metrics, no drawing);<br>• test tool `html2png <file.html> <width> <out.png>` | builds; PNG shows backgrounds, borders and table boxes; Mulberry unchanged | M |
| 2.2 | S1 fonts and text:<br>• CSS font description → fontconfig match (family list, generic families, weight, style, size);<br>• FreeType face and HarfBuzz font cache;<br>• metrics (ascent, descent, height, x-height);<br>• `text_width` by HarfBuzz shaping;<br>• `draw_text` with `cairo_show_glyphs`;<br>• underline, overline and strike-through | `html2png` on text samples: Latin, Cyrillic, ligatures; text measures and wraps correctly | L |
| 2.3 | Fallback per character: runs split by fontconfig coverage (as in the section 10.6 test), with a cache of fallback fonts | a mixed Latin/Cyrillic/Greek/Arabic/CJK sample has no missing glyphs | M |
| 2.4 | Colour emoji through Cairo's FreeType back end (CBDT, COLR) | emoji sample renders in colour, at the text's size | S–M |
| 2.5 | Test corpus in `Libraries/HTMLView/tests/`:<br>• anonymised newsletters and replies with quoted HTML;<br>• output of common clients (Outlook, Gmail, Thunderbird, Apple Mail).<br>`gmake check` renders all of them to PNG and reports time and memory | all render without crashing; timings comparable to section 3's measurements | M |
| 2.6 | Master style sheet and input limits:<br>• `script`, `iframe`, `object`, `embed`, `form` hidden;<br>• maximum HTML size, above which Mulberry falls back to the text view | hostile samples (deep nesting, huge tables, oversized input) end in bounded time without a crash | S–M |

**Stage 2 done (2026-11-23):** `Libraries/HTMLView` (see its `README.md`),
built with Mulberry; `html2png` renders any HTML file;
`gmake -C Libraries/HTMLView check` renders 13 test pages and 8 generated
stress pages. Findings and changes to the tasks above:
- **litehtml moved to master** (section 3): with v0.10, 30 nested tables did
  not finish within 60 s of CPU time.
- **Limits** (task 2.6), checked before parsing by a quick scan of the tags
  (Gumbo itself takes quadratic time in the nesting depth, so it cannot be
  used to measure it): nesting depth 512, 20,000 elements (litehtml uses
  about 12 KB and 30–60 µs per element: 12,000 table rows took 1.5 s and
  700 MB), 16 MB of HTML. A rejected mail is shown as text.
- **Hidden elements:** frames, embedded objects, media and form controls;
  the *content* of `<form>` stays visible (some newsletters wrap everything
  in a form), unlike the task's first wording.
- **Added for mail:** `cellpadding` (not mapped by litehtml; style rules for
  values 0–40, overridable by the mail's CSS) and `<ol type>` / `<ul type>`.
- **Text:** fallback per character, colour emoji (Twitter Color Emoji, the
  only colour font installed here; keycaps fall back to a text keycap),
  ligatures, all decoration styles, `text-transform` with Unicode case
  mapping, KOI8-R and windows-1252 pages detected through `<meta charset>`.
- **Known litehtml limitations:** `letter-spacing` and `word-spacing` are
  ignored; **no bidirectional text**: right-to-left words are shaped and
  joined correctly but stay in source order from left to right, and
  `dir="rtl"` is ignored. Fixing this needs a change in litehtml (a candidate
  for the fork, stage 9.4).
- Typical mail: 10–25 ms to parse and lay out, 3–25 ms to draw, 20–35 MB in
  total for the test tool (fonts included).

### 11.5 Stage 3: read-only viewer in JX (behind the preference)

| # | Task | Check | Size |
|---|---|---|---|
| 3.1 | JX fork (`freebsd` branch): public accessor for `JXWindowPainter::GetDrawable()`; update the submodule pointer | JX and Mulberry build | S |
| 3.2 | `CHTMLDisplay` (`Linux/Sources/Formatting`), a `JXScrollableWidget`:<br>• `Draw()` creates a Cairo Xlib surface on the drawable, using the visual from `JXDisplay`, then translates, clips and calls the core;<br>• resizing lays out again at the new width and sets the bounds to the document height.<br>Compiled only with `HTML_VIEWER`; not created anywhere yet | both configurations build; Mulberry unchanged | M |
| 3.3 | Preference "use the HTML viewer" (boolean, default off) in `CPreferences` and the preferences file; no dialog UI yet | old preference files load; the value is saved and read back | S |
| 3.4 | Preview pane (`CMessageView`):<br>• `CHTMLDisplay` created next to `mText` in `mTextFocus`, hidden;<br>• `ResetText()` shows it instead of `mText` when the preference is on, the view is formatted, the part is `text/html` and styles are on. The part's UTF-16 text is converted to UTF-8;<br>• the message header (`ParseHeader` text) becomes an escaped block at the top of the HTML document;<br>• `mText` keeps being filled as today (hidden), so Save, Print and reply quoting keep working on the text version;<br>• Find, Select All and Copy are disabled while the HTML view is shown (until stage 6) | preference off: nothing changes. On: HTML mail shows in the preview pane; plain-text mail, the plain and raw view modes, and switching messages all go back to the text widget; no command crashes | L |
| 3.5 | Message window (`CMessageWindow`): the same behaviour. Its `ResetText()` duplicates the preview pane's, so the shared logic moves into one helper used by both | same checks as 3.4, in a separate message window | M |
| 3.6 | Scrolling and keys, as in `CSpacebarEditView`:<br>• mouse wheel, Page Up/Down, Home/End;<br>• Space scrolls, then goes to the next message at the bottom;<br>• Tab goes to the next new message in the preview;<br>• Delete and Backspace behave as in the text view | the same keys work in both views | M |
| 3.7 | Font scale (`mFontScale`) becomes a zoom in the core; default font family and size from `mHTMLTextFontInfo` | zoom buttons work; the preference font is used | S |
| 3.8 | Redraw only the exposed area; check speed on the large newsletter | scrolling stays smooth; layout + paint below about 150 ms | S |

**Milestone after stage 3:** HTML mail is readable in Mulberry for testing
(preference on).

**Stage 3 done (2026-11-23; built, to be tried in use).** Changed by the
decisions taken then:
- **No preference (task 3.3 dropped):** the new view *is* the format popup's
  existing "as HTML" mode (`eViewAsHTML`), replacing the old flattening into
  styled text. A message with an HTML part (the main text part, or the HTML
  alternative of `multipart/alternative`, also inside `multipart/related`;
  not an HTML attachment) opens "as HTML", showing that part; a message
  without one opens "Formatted". A view the user picked (Plain Text, Source,
  as Enriched, ...) is kept from message to message, as before; a restored
  3-pane state keeps its view. This also covers most of stage 8.
- **The header area (addresses, subject, Parts and Format popups) is not
  affected.** The optional header text inside the message body (Header
  button, or the summary) is shown as a plain text block at the top of the
  HTML document (decision 3.4).
- **No JX change (task 3.1):** `JXWindowPainter::GetDrawable()` is read
  through a pointer to member taken in a derived class
  (`CHTMLDisplay.cpp`), so the JX fork stays untouched.
- `CHTMLDisplay` (`Linux/Sources/Formatting`): a `JXScrollableWidget` and a
  `CCommander` placed over the text widget in the preview pane
  (`CMessageView`) and in the message window (`CMessageWindow`); the one
  not in use is hidden. Cairo draws on the window's drawable (or its buffer
  pixmap), clipped to the area being redrawn. Layout again when the width
  changes; scroll position kept in the view state.
- Keys as in the text view: Space (page down, then next message;
  Shift/Control variants), Tab (next new message in the preview), Delete,
  Home/End, Page Up/Down, arrows, mouse wheel; single-key shortcuts go to
  the message view or window. The window's header fields pass Space and
  Delete to whichever view is shown.
- Default font family and size of the HTML view: the "HTML text" font
  preference with the font scale (+/- buttons), converted to pixels as Xft
  does (`Xft.dpi`, else the screen's resolution).
- The text widget is still filled (hidden) with the old rendering, so Save,
  Print, spell checking and reply quoting work as before until stages 6–7;
  Copy, Select All and Find are not available in the HTML view yet.
- If the viewer rejects a mail (limits of stage 2), the text widget shows it
  as before.
- `Libraries/HTMLView`: the result enum is `Document::Result`, not `Status`
  (Xlib defines `Status` as a macro).
- Build: `Linux/Makefile` links the static libraries (Mulberry grows by
  about 9 MB unstripped); run-time dependencies added: libpng and libXrender
  only. Without the viewer (`--disable-html-viewer`) the changed files
  compile to the old behaviour.

**Stage 3 follow-up (2026-11-23), from two real newsletters (an MJML one and
a classic table one):**
- Header text in the HTML view: one font size step smaller (÷1.2, as the
  font size buttons' "-1"); the other views are unchanged.
- Mail style sheet (`Libraries/HTMLView/src/Document.cpp`): `align="center"`
  / `"right"` on cells, `<div>`, `<p>` and `<center>` also centre (or
  right-align) child tables and block images, as browsers do
  (`-webkit-center`); newsletters centre their column this way.
- **litehtml patched** (in the submodule's working tree; needs an own fork,
  task 9.4, before it can be committed):
  - `render_table.cpp`: a cell's specified height (`height` attribute or
    CSS) is the minimum height of its row (CSS 2.1, 17.5.3). It was ignored,
    also in v0.10, so empty spacer cells took no space.
  - `table.cpp`: cells spanning columns are distributed in order of
    increasing span, as browsers do; a `colspan="8"` image cell over a
    `colspan="2"` price cell made each product card about 90 px too wide.
  - `render_block_context.cpp`, `render_inline_context.cpp`: vertical
    alignment in table cells (`middle` is the default) moves the content
    from where it is to where it belongs, instead of adding a shift each
    time the table is rendered again, and does nothing when the content
    measures as empty. Regression found by bisection: upstream `ebf1073`
    (2026-10-29, block content aligned in cells) with the layout cache of
    `28a806e` shifted MJML content by up to half a row and let the following
    content overlap it.
- New synthetic test page `tests/30-newsletter-layout.html` for these four
  cases. Real mail used for testing stays local (`tests/*.eml` is ignored).

### 11.6 Stage 4: links

| # | Task | Check | Size |
|---|---|---|---|
| 4.1 | Hover: hand cursor (`JXCursor`); the link target is shown, as in the text view | cursor and target follow the mouse | S–M |
| 4.2 | Click:<br>• `http(s):` and other URLs through the existing launch code (`CFormattedTextDisplay::LaunchURL`);<br>• `mailto:` opens the composer;<br>• `#name` scrolls inside the message | each kind works | M |
| 4.3 | Context menu "Open link" and "Copy link", same entries as the text view (`OnOpenLink`, `OnCopyLink`) | both entries work | S |

### 11.7 Stage 5: images and remote content

| # | Task | Check | Size |
|---|---|---|---|
| 5.1 | Image decoding in the core: PNG (libpng, already a Cairo dependency), JPEG (libjpeg-turbo), GIF first frame (giflib); WebP optional. Limits on bytes and pixels | `html2png` samples; oversized images rejected | M |
| 5.2 | `data:` URIs (base64 and percent-encoded) | sample with embedded images | S |
| 5.3 | `cid:` images:<br>• the core asks the host;<br>• Mulberry finds the sibling part with that Content-ID in the enclosing `multipart/related` (`CAttachment` tree, `CMIMEContent::GetContentId()`) and reads it with the existing `ReadAttachment` code;<br>• the core lays out again once sizes are known | Thunderbird and Apple Mail messages with inline images | M–L |
| 5.4 | Part selection: check that `FirstDisplayPart()` picks the `text/html` part inside `multipart/related` within `multipart/alternative`; inline images stay listed in the parts table as today | sample messages of each structure | S |
| 5.5 | Remote content blocked:<br>• no network access;<br>• placeholders sized from `width`/`height`;<br>• remote style sheets and fonts ignored;<br>• count of blocked items kept for 5.6 | no connection is attempted (checked with a local HTTP server log) | S |
| ~~5.6~~ | ~~Information bar "Remote content blocked – Load"~~: replaced by a button (stage R4 below) | | |
| ~~5.7~~ | ~~"Load" through Mulberry's HTTP client, synchronously~~: replaced by libcurl, asynchronous (stages R1–R5 below) | | |
| ~~5.8~~ | ~~Per-sender "always load" list~~: dropped (nothing is remembered, see below) | | |

**Stage 5, first part done (2026-11-24): images inside the mail.**
- Tasks 5.1–5.3 and 5.5 (counting only): `cid:` images (the part with that
  Content-ID anywhere in the message, inline or attached) and relative names
  (the part with that file name: Mulberry does not keep Content-Location),
  `data:` URLs; in `<img>`, CSS `background-image` and the `background`
  attribute. Remote images are never loaded; the viewer counts their
  distinct URLs (`Document::GetBlockedImages()`) for the information bar of
  task 5.6.
- **Decision 5.1 taken:** system libraries: libjpeg (libjpeg-turbo, also
  CMYK JPEGs) and giflib (first frame) required, libwebp optional
  (configure, `-DHTMLVIEW_WEBP`); PNG through Cairo. New run-time
  dependencies of Mulberry: libjpeg, libgif, libwebp.
- Parts are read synchronously while the document loads, through
  Mulberry's decoding filters and the mailbox (from the server if not
  cached), only for parts up to 24 MB encoded; the viewer refuses images
  over 16 MB encoded or 40 million pixels decoded.
- **Image parts the HTML does not refer to are shown below the message** (as
  Thunderbird does): a second document under the mail's, generated by
  `CHTMLDisplay` (each image under a rule and its file name, at most as wide
  as the view), so the mail's HTML and CSS cannot affect it. Only image
  parts the viewer can show (format and size limits checked from the
  header: `htmlview::IsDisplayableImage()`); the same size limits as above.
  Parts are read once per display (shared by both documents).
- Not shown yet: alt text of missing images; animated GIF/WebP (first
  frame / not shown); EXIF orientation of JPEGs. Parts of decrypted
  (S/MIME, PGP) messages are read from the mailbox, which may not hold
  their decrypted data: to check.
- Test page `tests/50-images.html` with generated images in `tests/images`
  (PNG, JPEG, CMYK JPEG, GIF, WebP with alpha; `cid:`, names, `data:`,
  backgrounds, remote, missing, broken).

**Stage 5, second part: remote images, design (decided 2026-11-24, not
implemented yet).**

*Behaviour*
- **Minimal GUI change** (the interface will be rewritten with another
  toolkit, probably FLTK): one new button, right of the Quotes popup ("All
  Quotes"), like the Headers button, in the 3-pane preview and in the
  message window. Shown only in the "as HTML" view, and only when the
  message refers to remote images.
  - Before loading: **"Load Remote Images: XX"**, XX = distinct remote image
    URLs of the message.
  - While loading: **"Loading Remote Images: n/XX"**, n = images received
    so far.
  - After loading: **"Loaded Remote Images: YY"**, YY = images downloaded,
    decoded and drawn (failures, refused formats and oversized images are
    not counted).
- **Loading on demand, per message:** a click loads the images of the
  message shown. **No preferences, no per-sender lists: nothing is
  remembered** about which messages or senders may load images.
- **Cache in memory only** (no disk cache for now), **128 MB**, keyed by URL,
  holding the downloaded (encoded) images; when it is full, the least
  recently used images are evicted. When a message is shown again in the
  same session (mail 1, then mail 2, then mail 1), its cached images are
  displayed at once, without network access; if all its remote images are
  cached, the button reads "Loaded Remote Images: YY", else it offers the
  missing ones. The size will become a preference later: a
  `TODO(remote-image-cache-size)` comment at the constant in the code.
- **Progressive display:** the message stays readable and scrollable; each
  image appears when it arrives; layouts are grouped (a few per second at
  most).
- Changing message cancels the downloads still running (images already
  received stay in the cache). Disconnected (offline) mode: the button is
  disabled; cached images are still shown.
- **Remote style sheets and fonts: never loaded** (unless really broken mails
  show they are needed). **Local-network addresses are not blocked.**

*Fetching: the system libcurl*
- libcurl's multi (non-blocking) interface, in the toolkit-independent core
  (`Libraries/HTMLView`): no threads; the host calls a short "pump"
  regularly (a JX idle task now, an FLTK timer later).
- Every request: `http` and `https` only, also after redirects (at most 5);
  no cookies, no `Referer`, no credentials (a 401 fails), a neutral
  `User-Agent`, `Accept` for images only; 16 MB per image, enforced while
  downloading; per message about 50 MB and 200 images; 10 s to connect, 30 s
  per image; TLS errors fail silently (no dialog); only PNG, JPEG, GIF and
  WebP, recognised by their content (never SVG).
- Build: `configure` checks `libcurl` (pkg-config) when the viewer is
  enabled; `build-freebsd.sh` already requires the `curl` package. Measured:
  libcurl 1 MB plus 2.2 MB of shared libraries Mulberry does not load yet
  (Kerberos, libssh2, libidn2, libnghttp2, libpsl); it uses the base
  system's OpenSSL, the same as Mulberry.
- **Logging:** when Mulberry's HTTP log is active (logging options, `HTTP`;
  `CLog::eLogHTTP`), libcurl's verbose output for these requests goes to it:
  the core passes it to the host through the fetcher's callbacks
  (`FetchHost::LogEnabled()` / `Log()`; `CURLOPT_VERBOSE` with a
  `CURLOPT_DEBUGFUNCTION` only when the log is on), and Mulberry writes it with a `CLog` of type
  `eLogHTTP`, one log session per message's load. Logged: libcurl's text
  messages and the request and response headers; not the image data.

*Stages, each leaving the build and the program correct*

| # | Task |
|---|---|
| R1 | Core: pending remote images (the document lists the remote URLs it skipped); a call handing it a downloaded image (decoded with the existing limits), telling whether a new layout or only a redraw is needed. `html2png -R DIR`: remote URLs mapped to local files, for tests without network |
| R2 | Core: the libcurl fetcher with the rules above and the log callback; `configure` check; `html2png -L` really downloads (manual tests). Tests against a local Python HTTP server: redirects, size cap, timeouts, refused protocols |
| R3 | Core: the memory cache (128 MB, least recently used out, TODO for the preference) |
| R4 | Mulberry: the button in both views; loading on click, progressive display, cancelling; cached images shown automatically; the HTTP log |
| R5 | Finishing: progress text, offline mode, per-message limits checked with real newsletters |

**R1 done (2026-11-24).** `htmlview::Document`: `GetRemoteImageURLs()` (the
distinct remote URLs in the order the document needed them; replaces the
count `GetBlockedImages()`), `SetRemoteImage(url, data)` (decoded with the
image limits; returns `ImageChange::NoChange`, `Redraw` or `Layout`) and
`GetLoadedRemoteImages()`. A new layout is needed when one of the image's
uses depends on its size (litehtml's `redraw_on_ready`: `<img>` without
`width`/`height`); backgrounds and sized images need only a redraw. A
`Layout()` after the images arrive lays the document out with their sizes
(litehtml renders afresh at each call). `html2png -R DIR` gives remote
images from files after the first layout; `tests/60-remote-images.html`
(run with `-R tests/images` by `run-tests.sh`).

**R2 done (2026-11-24).** `htmlview::Fetcher` (`include/htmlview/Fetcher.h`,
`src/Fetcher.cpp`): libcurl multi interface, `Add(url)`, `Pump()`, `Wait()`
(tools), `Cancel()`; results through `FetchHost::FetchDone(url, ok, data,
error)`; the network log through `FetchHost::LogEnabled()` / `Log()`
(libcurl's messages and request/response headers, prefixed `*`, `>`, `<` as
`curl -v` does; never data). All the request rules above as libcurl options
(`CURLOPT_PROTOCOLS_STR`/`REDIR_PROTOCOLS_STR` "http,https",
`DISALLOW_USERNAME_IN_URL`, `NETRC` ignored, no cookie engine, `MAXFILESIZE`
plus a check while writing, `FAILONERROR`, `PIPEWAIT` for HTTP/2); errors as
text ("HTTP 404", "larger than the size limit", libcurl's messages).
`configure` requires `libcurl` (pkg-config); `build-freebsd.sh` checks
`curl/curl.h`. `html2png -L` downloads, `-T` sets the time limit, `-V` prints
the network log. `gmake check-fetch`: 22 checks against a local Python HTTP
server (`tests/fetch-server.py`, `tests/fetch-tests.sh`), all passing; a real
HTTPS download (w3.org) checked by hand.

**Milestone after stage 5:** usable for daily reading.

### 11.8 Stage 6: selection, copy, find

| # | Task | Check | Size |
|---|---|---|---|
| 6.1 | Plain-text extraction of the rendered document (walks the render tree); enables Select All + Copy | copied text matches the visible text, block by block | S–M |
| 6.2 | Hit-testing: mouse position → text run and character offset (render tree, plus HarfBuzz cluster positions) | debug output on click | M |
| 6.3 | Selection:<br>• drag, double-click for a word;<br>• highlight drawing;<br>• copy as plain text through JX's selection manager | select, copy, paste into the composer | L |
| 6.4 | Reply and forward "Quote selection" use the HTML view's selection | quoting a selection works in both views | S–M |
| 6.5 | Find / Find Next on the extracted text: highlight the match and scroll to it | finds across blocks and table cells | M |

### 11.9 Stage 7: printing

| # | Task | Check | Size |
|---|---|---|---|
| 7.1 | Core: lay out at page width and render into a Cairo PostScript surface; page breaks by slicing, avoiding breaks inside a line of text; header block on page 1 | PS file from `html2png`'s print mode | M–L |
| 7.2 | Mulberry's Print command for the HTML view sends this PostScript to the existing print path (`SetPSPrinter`/`PrintPS`, `lpr`), with page setup | prints a newsletter correctly | M |

**Milestone after stage 7:** the same features as the text view.

### 11.10 Stage 8: user switch and default

*Mostly superseded (2026-11-23): "as HTML" in the format popup is the
switch, and it is the default for messages with HTML (stage 3 notes).*

| # | Task | Check | Size |
|---|---|---|---|
| 8.1 | Preferences dialog: checkbox "Display HTML messages formatted (new viewer)" | toggling it takes effect on the next message | S |
| 8.2 | Per-message switch (menu or toolbar) between the HTML viewer and the text rendering. If it is a new view mode, it goes at the end of `EView`, so saved window states stay valid | switching back and forth keeps the scroll position near the top | M |
| 8.3 | Default on, after a testing period | new preference files have it on; existing ones keep their choice | S |

### 11.11 Stage 9: packaging and follow-ups

| # | Task | Check | Size |
|---|---|---|---|
| 9.1 | Linux: `--with-system-harfbuzz` and `--with-system-cairo` (pkg-config). The default Linux behaviour is still to be discussed (section 10.7) | both builds work on a Linux machine | M |
| 9.2 | Documentation: README build section; licence texts installed and shown in About (litehtml BSD-3, Gumbo Apache-2.0, Cairo MPL-1.1, pixman MIT, HarfBuzz MIT) | texts present in the install | S |
| 9.3 | Optional fuzzing of the core (the `html2png` path) with mail corpora | no crashes after a set run time | M |
| 9.4 | Move the submodules to own forks when the first patch is needed, as for the other libraries | `git submodule sync` and a fresh-clone build | S |

### 11.12 Decisions to take before the stage concerned

| Decision | Options | Proposal |
|---|---|---|
| litehtml build (1.5) | litehtml's own CMake (new build tool `cmake`), or a small Makefile of ours | **Decided: CMake** (2026-11-23) |
| Image decoders (5.1) | system libjpeg-turbo + giflib (+ libwebp), or bundled `stb_image` (no releases, so the HEAD of its branch) | **Decided 2026-11-24:** system libraries (libwebp optional) |
| Fetching remote images (stage 5, R2) | system libcurl, a minimal static libcurl (submodule), or Mulberry's own HTTP code with a thread | **Decided 2026-11-24:** system libcurl |
| Remote images: what is remembered | per-sender list, preference, or nothing | **Decided 2026-11-24:** nothing; a button per message, memory cache of 128 MB (preference later) |
| Remote style sheets and fonts | load with the images, or never | **Decided 2026-11-24:** never (unless broken mails show the need) |
| Local-network addresses in remote images | block loopback/link-local, also private networks, or nothing | **Decided 2026-11-24:** nothing blocked |
| Message header (3.4) | escaped block inside the HTML document, or a separate text widget above the viewer | **Decided 2026-11-23:** inside the document; the header area of the window does not move |
| Default Linux HarfBuzz/Cairo (9.1) | bundled static or system shared | to be discussed (section 10.7) |
