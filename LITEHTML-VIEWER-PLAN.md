# HTML message display with litehtml: proposed fix

*Written 2026-11-20. Not implemented: a plan for deciding whether to add a
litehtml viewer to the current JX interface, to an FLTK port, or to both. It
complements `GUI-TOOLKIT-ANALYSIS.md` (section 7, option C, and section 9),
which is not changed by this document.*

## 1. Summary

Mulberry does not display HTML mail as HTML today. Its own parser flattens it
into styled text: no layout, no tables, no CSS, no images.

The proposal: a new **litehtml-based message viewer** for `text/html` parts,
drawing with Cairo and Pango, with images from the message itself, remote
content blocked by default, and Mulberry's existing link handling.

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
| fonts and text (`create_font`, `text_width`, `draw_text`) | upstream Cairo+Pango container: Pango gives fallback per character, emoji, right-to-left text and complex scripts |
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

## 5. Variant A: in the current JX interface

### Drawing

JX draws with Xlib and Xft and has no Cairo. Two ways to connect:

- **A1 (recommended): Cairo on the X drawable.**
  - In the viewer widget's `Draw()`, create a Cairo surface on the window's
    drawable (`cairo_xlib_surface_create(display, drawable, visual, w, h)`).
    With JX's double buffering this is the window's buffer pixmap
    (`JXWindow::itsBufferPixmap`), which Cairo can draw into like a window.
  - Translate to the widget's origin, clip to the update rectangle, and let
    litehtml draw through the **upstream Cairo+Pango container, unchanged**.
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
| Text in the viewer vs the rest of the UI | Pango vs Xft: two stacks, the viewer is better | Pango everywhere: consistent |
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
