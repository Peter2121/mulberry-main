# Mulberry GUI toolkit: analysis and options

*Written 2026-11-19, based on the `freebsd-build` branch of the Mulberry repository.*

## 1. Summary

Mulberry's Unix interface is built on **JX 2.5** (2005), a C++ toolkit drawing
directly with Xlib. Besides looking dated (Motif-style bevels), it has functional
limits: no text outside Latin-1, no HiDPI scaling, X11 only.

Options examined: **JX 4.2** (current upstream), **EFL**, **FLTK 1.4**,
**Qt 6**, plus wxWidgets, GTK4 and web-based UIs.

- **Recommended: Qt 6 (Qt Widgets)**, ported incrementally as a new UI layer next
  to the existing one. It is the only option that solves the look, Unicode,
  HiDPI, Wayland, accessibility and rich text (including inline images) at once,
  and one Qt UI could later replace the dead macOS and untested Windows layers.
- **Credible alternative: FLTK 1.4**, if minimal dependencies and the fastest
  mechanical port matter more than a native look. It is the closest to the
  current code (JX's layout files are XForms files, and FLTK grew out of
  XForms), but rich text with inline images must be built on top (section 7).
- **Not recommended:** JX 4.2 (big port, still looks old) and EFL (C API,
  unstable C++ API, weak rich text).
- **HTML mail display:** litehtml rather than Qt WebEngine, with either
  toolkit. Measured: 25–47 MB versus 225–243 MB for the same mails, and 1.3 MB
  of code versus a 209 MB library (section 9).

All effort figures in this document are rough estimates for one experienced
developer, with low confidence. A 1–2 week prototype would turn them into
measurements (section 10).

## 2. Current state (measured)

| Layer | Lines of code | State |
|---|---|---|
| `Sources_Common`: protocols, model, formatting, `*Common` view logic | 267,000 | shared, actively maintained |
| `Linux/Sources`: JX 2.5 user interface | 190,000 | the only living UI (135 commits in 3 years) |
| `MacOS/Sources`: PowerPlant on Carbon | 191,000 | **dead**: Carbon's UI never went 64-bit, cannot be built on any current macOS |
| `Win32/Sources`: MFC | 187,000 | 44 commits in 3 years, untested |
| `Libraries` (CICalendar, vCard, XMLLib) | 30,000 | shared |
| `Plug-ins` | 47,000 | shared |

### Architecture (favourable for a port)

- **Clean separation:** only 16 files in `Sources_Common` include toolkit headers
  (JX, MFC or PowerPlant).
- **Shared logic with thin platform subclasses**, e.g. `CMailboxTableCommon`,
  `CMailboxViewCommon` and `CMailboxWindowCommon` in `Sources_Common`, under
  `CMailboxTable`, `CMailboxView` and `CMailboxWindow` in `Linux/Sources`.
- **The Linux UI sits on its own small framework.** A port reimplements these
  classes and keeps their interfaces, instead of rewriting all 190,000 lines one
  by one:

  | Mulberry class | What it is | Subclasses |
  |---|---|---|
  | `CDialogDirector` | dialog base | 62 |
  | `CTabPanel` | tab-page base | 44 |
  | `CTable`, `CTableDragAndDrop`, `CHierarchyTableDrag` | tables with drag and drop | |
  | `CTextDisplay` | styled text view | |
  | `CToolbar` | toolbars | |
  | `CCommander` | command routing | |
  | `CPrefsPanel` and related | preference panels | |

- **Text model is toolkit-friendly:** the core uses `cdstring` (UTF-8) and
  `cdustring` (UTF-16). The formatting engine (`Sources_Common/Formatting`)
  turns HTML, enriched and plain text into styled runs with clickable elements.

### Size of the UI work

- 208 `.fd` layout files, generating 458 dialog and panel layouts.
- Biggest areas (lines): Calendar 26,000, Preferences 25,000, Message 11,000,
  Letter (composer) 10,000, Address Book 10,000, Server 10,000, Three-pane
  window 8,000, Rules 7,000.
- Most used JX classes: `JXWidget` (6,570 references), `JXStaticText`,
  `JXContainer`, `JXTextCheckbox`, `JXTextButton`, `JXWindow`, `JXMenu`,
  `JXScrollableWidget`, `JXTEBase16`.

### Functional limits of JX 2.5 (not just looks)

- No text outside Latin-1, hence the emoji-to-ASCII workaround described in the README.
- No HiDPI scaling, X11 only, Motif-style 3D bevels.
- Mulberry carries its own JX patches (`JString16`, `JXTEBase16`, clipboard
  fixes) that never existed upstream.
- Uses ACE 5.4.7 (2005); Mulberry itself uses ACE only for threads and mutexes
  (13 files).

### Rich text today

- **Display:** HTML, enriched and plain text are converted to styled runs:
  fonts, sizes, colours, bold/italic/underline, alignment, quoted blocks, links.
- **Images are not displayed:** `CParserHTML::HandleImage` outputs only the `ALT` text.
- **Composer:** styled text, no images.
- An external-editor preference exists (`mUseExternalEditor`).

Inline images are therefore a **new feature** whichever toolkit is chosen.

## 3. Option: JX 4.2 (current upstream)

Upstream JX (github.com/jafl/jx_application_framework) is alive: commits from
August 2026, maintained by its original author, LGPL.

**It brings:**
- UTF-8 support;
- an event model built on fibers (Boost);
- a new layout editor (`.jxl` files) replacing `.fd`;
- a menu editor.

**It doesn't change:**
- **The look:** still Motif-style raised and sunken frames, still 75 dpi font logic.
- **Displays:** no HiDPI, no Wayland.
- **ACE:** still required.
- **FreeBSD:** its `configure` is a bash script that knows only Linux
  distributions and macOS.

**Breaking changes since 2.5:** 55 in libjx and 67 in libjcore, as explicitly
marked in the changelogs. They include:
- "Completely redesigned JString API": 1,368 uses in Mulberry, plus 334 uses
  of the Mulberry-only `JString16`;
- a text-editor rewrite: 927 uses;
- all dialogs now block, which affects the 62 dialog classes;
- `.fd` layouts replaced by `.jxl`.

**Verdict:** the port would touch most of the 190,000 lines (about 3–6
person-months) for Unicode, but the result would still look old-school, run on
X11 only, and depend on a toolkit with essentially one maintainer.

## 4. Option: EFL (Elementary)

- **API:** the stable widget API is C (`elm_genlist`, `elm_entry`, in
  `*_legacy.h` headers). The C++ bindings cover the newer `Efl.Ui` API, where
  125 of the 304 installed C++ widget headers require `EFL_BETA_API_SUPPORT`,
  i.e. unstable API (checked in the installed EFL 1.29).
- **Missing pieces Mulberry needs:**
  - a mature rich-text editor (the `elm_entry` markup editor is basic);
  - sortable, resizable multi-column tables;
  - a dialog designer;
  - thorough documentation.
- **Ecosystem:** a small core team with roughly yearly releases (1.28, then
  1.29 in October 2026, per secondary sources).
- **Platforms:** Linux/BSD first; not a realistic route to Windows/macOS.
- **Look:** Edje-themed, native in Enlightenment but foreign elsewhere.

**Verdict:** the highest risk, and likely more effort than Qt.

## 5. Option: FLTK 1.4

### Facts (checked in the 1.4.5 sources and FreeBSD packages)

- **Releases:** 1.4.5 is current (25 April 2026). 1.4 is in maintenance mode;
  1.5 is in development, planned to be API-compatible with 1.4, CMake-only,
  possibly C++17.
- **Licence:** LGPLv2 with an exception allowing static linking.
- **FreeBSD:** package `fltk-1.4.5`, built with Wayland on and Cairo off.
  The installed copy on the development machine is the older 1.3.10.
- **Display:** X11 and Wayland back ends; HiDPI with a scale factor per screen;
  UTF-8 throughout; Windows and macOS back ends.
- **Widgets:**

  | Widget | What it is |
  |---|---|
  | `Fl_Table` | virtual table: draws only visible cells, good for huge mailboxes |
  | `Fl_Tree` | tree view |
  | `Fl_Flex`, `Fl_Grid` | layout managers (new in 1.4) |
  | `Fl_Native_File_Chooser` | desktop file dialog |
  | `Fl_Printer` | printing |
  | `Fl_Text_Display`/`Fl_Text_Editor` | styled text viewer/editor |
  | `Fl_Help_View` | small HTML viewer |
  | FLUID | dialog designer |

- **Look:** built-in schemes (`base`, `plastic`, `gtk+`, `gleam`, `oxy`) and
  anti-aliased fonts. No native theme, no automatic dark mode (colours set by hand).

### Why FLTK fits Mulberry's current code well

- **Same programming style as JX:** plain C++ widget classes, absolute
  positioning, self-drawn widgets, one event loop. Most classes map one to one:

  | JX | FLTK |
  |---|---|
  | `JXWidget` | `Fl_Widget` |
  | `JXWindow` | `Fl_Window` |
  | `JXTextButton` | `Fl_Button` |
  | `JXTextCheckbox` | `Fl_Check_Button` |
  | `JXTable` | `Fl_Table` |
  | `JXTextMenu` | `Fl_Menu_` |
  | `JXScrollbarSet` | `Fl_Scroll` |
  | `JBroadcaster` messages | callbacks |

- **The layout files are XForms files, and FLTK grew out of XForms.**
  - Mulberry's 208 `.fd` files use XForms classes (`FL_TEXT` 731×, `FL_BOX`
    552×, `FL_CHECKBUTTON` 547×, `FL_BUTTON` 434×, `FL_INPUT` 223×, …).
  - They use box types (`FL_UP_BOX`, `FL_FLAT_BOX`, `FL_NO_BOX`, `FL_DOWN_BOX`)
    and `FL_ALIGN_*` constants that FLTK still defines under the same names.
  - A converter from `.fd` to FLUID's `.fl` format, or straight to C++, would be
    close to one to one.
- **Footprint:** small, fast to build, no code generator, static linking allowed.

### Weaknesses for an email client

1. **Rich text.**
   - `Fl_Text_Display` gives every line the height of the tallest style in use
     (`mMaxsize` is computed over the whole style table), so one heading makes
     the whole text double-spaced.
   - No inline images, no paragraph alignment or indentation, no tables.
   - Styles are a table indexed by one byte per character.
   - `Fl_Help_View` supports only an HTML 3.2-style subset (no CSS, `DIV`,
     `SPAN`, `BLOCKQUOTE`).
   - See section 7 for the ways around this.
2. **Missing glyphs on X11.** Without Pango (as in the FreeBSD package), the X11
   path draws each string with **one** Xft font and no per-character fallback,
   so CJK or emoji the font lacks appear as boxes. Pango (Wayland, or FLTK built
   with `FLTK_USE_PANGO=ON`) fixes this and adds right-to-left/complex-script shaping.
3. **No accessibility at all** (no AT-SPI, UI Automation or NSAccessibility code).
4. **Clipboard and drag and drop:** only plain text and images; no custom data types.
5. **Look:** cleaner and sharper than JX with `gleam`/`oxy`, but still FLTK's own plain look.
6. **Smaller ecosystem:** no table model framework, no spell-check widget, no
   translation framework, no HTML engine.

**Effort:** about 6–12 person-months. Dialogs, menus and tables are the
cheapest of all options; the message view and composer are the most expensive.

## 6. Option: Qt 6, Widgets (recommended)

- **Platform:** Qt 6.11.3 is in FreeBSD packages (`qt6-base`); Qt 6.12 LTS was
  released in September 2026. Qt is KDE's foundation and well tested on FreeBSD.
- **Look and display:** modern native look (Fusion or the desktop's style), dark
  mode, HiDPI, X11 and Wayland, accessibility through AT-SPI.
- **Text:** full Unicode including emoji and right-to-left scripts, so the
  emoji-to-ASCII workaround can be deleted.
- **How Mulberry maps onto Qt:**

  | Mulberry needs | Qt provides |
  |---|---|
  | mailbox lists sorted, filtered and threaded on the server | model/view tables loading rows lazily (`QAbstractItemModel`) |
  | styled-run formatting engine | `QTextDocument`; `QTextBrowser`/`QTextEdit` for viewing and composing, with inline images |
  | full HTML mail display | litehtml with a `QPainter` container (not Qt WebEngine: see section 9) |
  | dialog layouts | Qt Designer `.ui` files with HiDPI-safe layouts |
  | single-instance check (JX "MDI server") | `QLocalServer` |
  | custom drawing in calendar views, printing, drag and drop, clipboard | `QPainter`, `QPrinter`, Qt drag and drop, `QClipboard` |

- **Strategic gain: one UI instead of three.** The same layer later builds on
  macOS and Windows, replacing the dead PowerPlant layer and the untested MFC one.
- **Licence:** LGPLv3, compatible with Mulberry's Apache-2.0 licence when Qt is
  linked dynamically.
- **Costs:**
  - the largest rewrite, although Qt's richer widgets remove most of
    `Linux/Sources/Support` (about 30,000 lines);
  - Qt's code generator (moc), and practically a move to CMake for the new layer.
- **Effort:** about 8–15 person-months to reach full parity.

## 7. Rich text with inline images under FLTK

### Requirements

**Viewer:**
- show images from parts of the same message (`cid:`), from `data:` URIs, and
  from image attachments;
- block remote images by default;
- selection, copy, find, HiDPI.

**Composer:**
- insert images by file, drag or paste;
- send `multipart/related` with `cid:` references;
- undo/redo, input methods (IME), spell-check underline, quoting.

### FLTK 1.4 building blocks

- **Clipboard:** plain text and images only (pasting screenshots works, HTML
  from browsers does not).
- **Input methods:** `Fl::compose`, `Fl::insertion_point_location`,
  `Fl::reset_marked_text`.
- **Images:** PNG, JPEG, GIF (including animated), SVG, BMP and others; no WebP
  (would need `libwebp`).
- **Cairo:** `Fl::cairo_make_current`; the Wayland back end already draws
  everything with Cairo and Pango.
- **No existing FLTK rich-text editor was found.**

### Option A: patch `Fl_Text_Display`/`Fl_Text_Editor` (FLTK fork)

- **Approach:** per-line heights; each image as one special character (U+FFFC)
  whose style says "draw image N"; paragraph alignment.
- **Pro:** reuses FLTK's selection, keyboard, undo and input-method handling.
- **Cons:**
  - the fixed line height is assumed everywhere (`mMaxsize` is used 21 times in
    a 4,481-line file; scrolling counts lines);
  - variable heights mean rewriting the line cache, scrolling and hit-testing;
  - no shaping or font fallback on X11 without Pango;
  - a permanent fork.
- **Effort:** about 2–3 person-months, medium-high risk.

### Option B: custom rich-text widget on Pango + Cairo (best for the composer)

- **Model:** a list of paragraphs, each with styled text runs and inline
  objects. Maps directly onto Mulberry's `CFormatList` (`CFontFormatElement`,
  `CColorFormatElement`, `CAlignmentFormatElement`, `CExcerptFormatElement`, …).
- **Layout:** one Pango layout per paragraph; images reserve space through a
  Pango "shape" attribute (`pango_attr_shape_new_with_data`) and are drawn at
  the position Pango reports; alignment, indent and wrapping from Pango.
- **Editing primitives, all from Pango** (checked in the installed headers):

  | Need | Pango function |
  |---|---|
  | hit-testing (mouse position → text position) | `pango_layout_xy_to_index` |
  | caret position (handles bidirectional text) | `pango_layout_get_cursor_pos` / `get_caret_pos` |
  | arrow-key movement | `pango_layout_move_cursor_visually` |
  | word/character boundaries | `pango_layout_get_log_attrs` |

- **FLTK supplies** events, input-method hooks, clipboard, file drops, Cairo context.
- **Output:** HTML plus a plain-text alternative, as `multipart/related`.
- **Pros:** full Unicode with font fallback, emoji, right-to-left, HiDPI; one
  engine for viewing and composing; proven design (GTK's text widget works this way).
- **Cons:**
  - the editor logic must be written (selection, undo, clipboard, input
    methods, relayout, scrolling, spell-check squiggles), estimated
    8,000–15,000 lines;
  - FLTK must be built with Cairo and Pango;
  - Pango on Windows/macOS is possible but heavy;
  - no accessibility.
- **Effort:** about 3–5 person-months, medium risk.
- **Variant:** HarfBuzz + FreeType + libraqm with own line breaking avoids GLib
  but means more work. Not worth it.

### Option C: litehtml for the viewer (best for displaying HTML mail)

- **What it is:** an HTML/CSS layout engine (BSD-3, about 44,000 lines), very
  active (commits on 18 November 2026), no JavaScript, used in production (e.g.
  Qt Assistant).
- **Integration:**
  - it draws nothing itself; the application provides a drawing interface
    (`create_font`, `draw_text`, `load_image`, `draw_image`, …);
  - upstream ships ready **Cairo** and **Cairo+Pango** implementations;
  - `load_image` is the single point for resolving `cid:`/`data:` images and
    blocking remote ones.
- **Pros:** renders real HTML mail (table-based newsletters, CSS), an upgrade
  over Mulberry's current flattening to styled text; small and secure.
- **Cons:** read-only; selection, copy and find must be added; incomplete CSS
  (fine for email's table layouts); not packaged on FreeBSD (vendor as a submodule).
- **Measured footprint:** 1.3 MB of code; 25 MB for a small mail and 47 MB for a
  large newsletter, including Pango/Cairo (section 9).
- **Effort:** about 1–1.5 person-months, low risk.

### Option D: embed WebKitGTK (like Evolution's composer)

- **Pros:** full HTML/CSS; WYSIWYG editing with images (`contenteditable`);
  spell check, input methods and accessibility inside the web view; packaged on
  FreeBSD (`webkit2-gtk` 2.46).
- **Cons:**
  - two toolkits and two event loops;
  - embedding works only on X11 (Wayland doesn't allow embedding another
    application's window);
  - about 180 MiB of dependencies;
  - large attack surface (JavaScript and remote loading must be locked down);
  - no route to Windows/macOS.
- **Verdict:** possible but architecturally ugly; contradicts choosing FLTK.

### Option E: Skia with its paragraph module (`SkParagraph`)

- Like option B, with Skia: placeholders for inline images, shaping via
  HarfBuzz/ICU; render to an image (`fl_draw_image`) or via OpenGL.
- **Pro:** identical rendering on all platforms.
- **Cons:** heavy build (Skia's tools, ICU); fewer editing helpers than Pango;
  the FreeBSD `skia` package was not confirmed to include the paragraph module.
- **Effort:** about 4–6 person-months.

### Option F: Markdown composer with live preview (cheapest)

- Compose in `Fl_Text_Editor` with Markdown syntax highlighting.
- Dropping or pasting an image inserts `![alt](cid:img1)` and attaches the part.
- A preview pane uses the viewer (C or B).
- On send, `md4c` or `cmark-gfm` (both packaged) produce the HTML part; the
  Markdown source is the plain-text alternative.
- **Pros:** works with FLTK as-is; about 2–4 weeks; robust.
- **Cons:** not WYSIWYG; replying to rich HTML is awkward (lossy conversion or
  an opaque quoted block).

### Option G: extend `Fl_Help_View` (stopgap viewer)

- FLTK's HTML-3.2-style viewer (3,767 lines) already supports `IMG` and `TABLE`;
  no CSS, no editing.
- Fine as an interim viewer; extending it seriously reinvents litehtml.
- **Effort:** 1–2 weeks.

### Option H: external editor (complement only)

Already in Mulberry's preferences; fine for plain text or Markdown, no
inline-image experience.

### Comparison of rich-text options

| Option | Role | WYSIWYG editing | Images | Real HTML/CSS display | Unicode, right-to-left, font fallback | New dependencies | Wayland | Effort | Risk |
|---|---|---|---|---|---|---|---|---|---|
| A: patch `Fl_Text_*` | view + edit | ✓ (limited) | ✓ | ✗ | only with Pango | none (FLTK fork) | ✓ | 2–3 person-months | medium-high |
| **B: Pango/Cairo widget** | view + edit | ✓ | ✓ | ~ (Mulberry's parser) | ✓ | Pango, Cairo | ✓ | 3–5 person-months | medium |
| **C: litehtml** | view | ✗ | ✓ | ✓ | ✓ (Pango back end) | litehtml (vendored) | ✓ | 1–1.5 person-months | low |
| D: WebKitGTK | view + edit | ✓ | ✓ | ✓ | ✓ | about 180 MiB, GTK | ✗ (X11 only) | 2–3 person-months, ongoing pain | high |
| E: Skia | view + edit | ✓ | ✓ | ~ | ✓ | Skia, ICU | ✓ | 4–6 person-months | medium-high |
| **F: Markdown + preview** | edit | ✗ | ✓ | (via C/B) | ✓ | `md4c` | ✓ | 2–4 weeks | low |
| G: `Fl_Help_View` | view (stopgap) | ✗ | ✓ | ~ (HTML 3.2) | X11 issues | none | ✓ | 1–2 weeks | low |

### Recommended rich-text path if FLTK is chosen

1. **Build FLTK yourself with Pango and Cairo** (`FLTK_USE_PANGO=ON`,
   `FLTK_GRAPHICS_CAIRO=ON`, `FLTK_OPTION_CAIRO_EXT=ON`), vendored like JX
   today. This fixes the missing-glyph boxes everywhere, gives right-to-left and
   complex-script support, and is the base for B and C.
2. **Viewer:** litehtml (C) with its upstream Cairo+Pango back end.
3. **Composer, in stages:** first F (Markdown plus preview) for inline images
   quickly; then B (Pango/Cairo WYSIWYG) as the long-term editor. Both share the
   Pango/Cairo stack and the image pipeline (`Fl_Shared_Image`, plus `libwebp`,
   scaled for HiDPI).

**Total:** about 4–6 person-months for full WYSIWYG, or about 2 months for
viewer plus Markdown composer. In Qt, the editor and viewer come almost free
(`QTextEdit`/`QTextDocument` handle images; litehtml for HTML display, see
section 9). This rich-text work is the main reason FLTK's total effort ends up
close to Qt's.

## 8. Overall comparison

| | JX 4.2 | EFL 1.29 | FLTK 1.4 | **Qt 6** |
|---|---|---|---|---|
| Modern look | ✗ Motif bevels | ~ Enlightenment look | ~ own plain styles, not native | ✓ native or Fusion |
| Follows desktop theme / dark mode | ✗ | ~ in Enlightenment only | ✗ manual colours | ✓ |
| Unicode / emoji | ✓ | ✓ | ~ boxes on X11 without Pango | ✓ |
| Right-to-left / complex scripts | ? | ✓ | ~ only with Pango | ✓ |
| HiDPI | ✗ | ✓ | ✓ | ✓ |
| Wayland | ✗ (XWayland only) | ✓ | ✓ | ✓ |
| macOS / Windows from the same code | ~ / ✗ | weak | ✓ | ✓ |
| Accessibility (screen readers) | ✗ | ~ | ✗ none | ✓ |
| Rich-text view and editor, inline images | partial | weak | ✗ must be built (section 7) | ✓ |
| Large-mailbox tables | ✓ | ✓ | ✓ `Fl_Table` | ✓ model/view |
| Drag and drop, clipboard | ✓ | ✓ | ~ text and images only | ✓ any data type |
| Closeness to today's JX code | same family, 122 breaking changes | ✗ C API | ✓ closest (XForms lineage) | medium |
| C++ API | ✓ | C (C++ API is beta) | ✓ | ✓ |
| Dependencies / footprint | ACE, Boost | large | minimal, static linking OK | large (moc, CMake) |
| FreeBSD package | ✗ | ✓ | ✓ (own build with Pango advised) | ✓ |
| Maintainers | ~1 person | small team | small, steady team | company + KDE |
| Port effort (person-months) | ~3–6, little visible gain | very high | ~6–12 | ~8–15 |

### Other toolkits considered and rejected

| Toolkit | Why rejected |
|---|---|
| wxWidgets 3.2/3.3 | MFC-like API with MFC porting guides, but that means starting from the untested Windows layer and losing the Linux fixes; weaker rich text; uses GTK3 on Unix. |
| GTK4/gtkmm | GNOME-centric, weak on Windows/macOS, frequent API churn. |
| Electron/Tauri (web-based UI) | Not appropriate for this codebase. |

### Polishing JX 2.5 instead

Flat frames, a new palette and a new icon set are possible in days, but that is
effort spent on code that will be replaced, and it doesn't fix Unicode or
HiDPI. At most, adjust default fonts.

## 9. HTML message viewer: litehtml vs Qt WebEngine (measured)

Both toolkits need an engine to display HTML mail. The two realistic choices
are **litehtml** (section 7, option C; usable with FLTK or Qt) and **Qt
WebEngine** (Chromium, Qt only). Both were measured on 2026-11-20 on the
development machine (FreeBSD 15.1, amd64).

### Method

- **Test mails:**
  - a typical small HTML mail: 1.7 KB, paragraphs, a list, a quoted reply;
  - a large newsletter: 83 KB, nested tables, a `<style>` block, inline
    styles, rounded badges, 120 product blocks; laid out at 800×16,000 px.
- **litehtml:** upstream master of 2026-11-20 (commit `efb0ec8`), built with
  GCC 14 (`MinSizeRel`), statically linked with its upstream Cairo+Pango
  container. Parse, lay out, and paint the visible 800×1000 viewport into a
  Cairo image surface. The rendering was checked visually: CSS classes,
  borders, rounded corners, colours, table layout and font shaping were correct.
- **Qt WebEngine:** 6.11.2 from FreeBSD packages, a minimal `QWebEngineView`
  (800×1000) calling `setHtml()`, measured 1.5 s after `loadFinished`, with
  `QT_QPA_PLATFORM=offscreen` and GPU disabled
  (`QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu`).
- **Memory:** from `procstat -v` for the program and all its child processes:
  private (anonymous) resident pages per process, plus file-backed pages
  (shared libraries) counted once per file. Plain RSS sums would count
  WebEngine's 209 MB library twice (it is mapped in both of its processes); they
  gave 532–542 MB.

### Results

| | **litehtml** (Cairo/Pango) | **Qt WebEngine** (`QWebEngineView`) |
|---|---|---|
| Code added to the application | **1.3 MB**, statically linked (Pango/Cairo come with any desktop) | `libQt6WebEngineCore.so` alone **209 MB**; package 293 MiB |
| Dependencies | litehtml (vendored) + Pango, Cairo, fontconfig | full dependency closure: 215 packages, 4.5 GB installed (Qt base and QML, ffmpeg, mesa, NSS, pipewire, …) |
| Processes | 1, inside the application | 2: application + Chromium renderer (+1 GPU process when GPU is enabled) |
| Memory, small mail | **25 MB** (7 MB private) | **225 MB** (44 MB private) |
| Memory, large newsletter | **47 MB** (30 MB private) | **243 MB** (63 MB private) |
| Time to display, small / large | 37 ms / 124 ms | 415 ms / 658 ms |

For reference: a plain Qt Widgets window (a `QLabel`) measured the same way
uses 34 MB, and Pango/Cairo/fontconfig drawing one paragraph uses 22 MB (peak
RSS). Today's Mulberry (JX) uses about 100 MB in normal use.

If another WebEngine application already runs on the desktop, its shared
libraries are paid once system-wide, and WebEngine's additional cost drops to
roughly its private 44–63 MB: still about 6× litehtml's.

### Capabilities

| | litehtml | Qt WebEngine |
|---|---|---|
| HTML/CSS | HTML + CSS 2.1, much of CSS 3 (flexbox, gradients, partial grid): enough for email, which is mostly table layouts with inline styles | complete modern web platform |
| JavaScript | none, ideal for mail | present, must be disabled for mail |
| Remote content / privacy | the application decides every image load (one `load_image` callback) | needs request interception (`QWebEngineUrlRequestInterceptor`) |
| Security updates | small code base, but less fuzz-tested | Chromium: frequent security fixes, so Qt WebEngine updates are mandatory |
| Selection, find, accessibility | to be written (selection/find about 1 person-month; no accessibility) | built in |
| Platforms | wherever Cairo/Pango or a Qt painter is available | Qt platforms where Chromium builds; on FreeBSD it depends on the ports' patches |

### Conclusion

- **Use litehtml for displaying HTML mail**, with FLTK or Qt alike. It keeps
  Mulberry in its current memory class; a WebEngine viewer would roughly
  triple the footprint and bring Chromium's security-update cadence, for
  features (JavaScript, web applications) a mail viewer must switch off anyway.
- **In a Qt port,** litehtml draws through a small `QPainter`-based container
  instead of Cairo/Pango (community Qt containers exist); the footprint stays
  similar, with Qt's text stack replacing Pango.
- **The only strong reason for WebEngine** would be a WYSIWYG composer using
  HTML's editable mode (section 7, option D). Under Qt that is not needed:
  `QTextEdit` covers composing.

## 10. Proposed plan (Qt)

0. **Preparation, independent of any toolkit (weeks):**
   - replace ACE in `Sources_Common` (13 files, threads and mutexes only) with
     `std::thread`/`std::mutex`;
   - separate "operating system" from "UI toolkit" in the 1,929 platform `#if`
     conditions (today `__linux_os` means both "Unix" and "JX");
   - split the 16 shared files that include toolkit headers.
1. **Qt skeleton** in a new `Qt/` directory next to `Linux/` (JX stays buildable):
   - reimplement the Mulberry base classes (`CDialogDirector`, `CTable`,
     `CTextDisplay`, `CToolbar`, `CCommander`) on Qt;
   - build a read-only three-pane window (accounts, mailbox list, message view)
     on the existing `*Common` logic, to prove the architecture early; HTML
     messages are displayed with litehtml (section 9).
2. **Composer** (`QTextEdit`, with inline images), drag and drop, menus and
   toolbars mapped onto `QAction`.
3. **Dialogs:** a converter from `.fd` to Qt Designer `.ui` for the 208 layout
   files (fixed positions first, layouts gradually); then Preferences, Rules,
   Search, Address book, Calendar (custom-drawn with `QPainter`).
4. **Retire JX, ACE and `Linux/`.** Later, macOS and Windows builds from `Qt/`,
   and removal of `MacOS/` and `Win32/`.

**First step:** a 1–2 week trial covering the ACE removal, a Qt skeleton
showing the mailbox list from a real IMAP account, and a prototype `.fd` →
`.ui` converter, to turn the estimates above into measurements.

If FLTK is chosen instead, the same steps 0 and 1 apply, with a `.fd` → `.fl`
converter in step 3 and the rich-text path of section 7.

## 11. Sources

- [JX Application Framework (GitHub)](https://github.com/jafl/jx_application_framework), [libjx on SourceForge (licence)](https://sourceforge.net/projects/libjx/)
- [Qt 6.12 LTS released (Phoronix)](https://www.phoronix.com/news/Qt-6.12-LTS-Released), [FreshPorts devel/qt6](https://www.freshports.org/devel/qt6/)
- [Enlightenment 0.28 / EFL 1.29 (OMG! Ubuntu)](https://www.omgubuntu.co.uk/2026/11/enlightenment-0-28-features), [EFL news](https://enlightenment.org/news)
- [FLTK roadmap](https://www.fltk.org/roadmap.php), [FLTK 1.4.5 release announcement](https://groups.google.com/g/fltkcoredev/c/_oY4fKlTYxw), [RFC: FLTK 1.4 maintenance mode and 1.5 development](https://groups.google.com/g/fltkcoredev/c/7oADtBkmLGw), [FLTK 1.4 text editor documentation](https://www.fltk.org/doc-1.4/editor.html)
- [litehtml (GitHub)](https://github.com/litehtml), [litehtml attribution in Qt 6 docs](https://doc.qt.io/qt-6/qtassistant-attribution-litehtml.html)
- [wxWidgets 3.3.0 released](https://www.wxwidgets.org/news/2025/06/wxwidgets-3.3.0-released/), [wxWidgets MFC porting guide](https://www.wxwidgets.org/news/2023/10/mfc-porting-guide)
- Verified locally: Mulberry sources, JX 4.2 upstream, FLTK 1.4.5 sources (tag
  `release-1.4.5`), litehtml upstream, installed EFL 1.29 and Pango headers,
  FreeBSD package listings.
- Measured locally (section 9): litehtml upstream commit `efb0ec8` with its
  Cairo+Pango container, Qt WebEngine 6.11.2 from FreeBSD packages.
