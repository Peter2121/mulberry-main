# HTMLView: Mulberry's HTML viewer core

Lays out and draws HTML mail with [litehtml](https://github.com/litehtml/litehtml),
Cairo and HarfBuzz (stack S1 in `LITEHTML-VIEWER-PLAN.md`; this is stage 2
of its section 11). It does not depend on JX or on the rest of Mulberry: the
host gives it HTML, a width and a Cairo context, and receives callbacks.

| File | Content |
|---|---|
| `include/htmlview/Document.h` | public interface: `Settings`, `Host`, `Document` |
| `src/Document.cpp` | loading (with the limits below), layout, drawing; style sheet additions for mail |
| `src/Container.*` | the litehtml container: litehtml's own `container_cairo` (borders, backgrounds, gradients, list markers) plus our text |
| `src/Fonts.*` | fonts and text: fontconfig, fallback font per character, HarfBuzz shaping, Cairo drawing |
| `src/TextTransform.*` | CSS `text-transform` with Unicode case mapping (libunistring) |
| `src/Limits.*` | quick scan of the HTML before parsing: nesting depth and element count |
| `src/Images.*` | image decoding (PNG, JPEG, GIF, WebP) and `data:` URLs |
| `tools/html2png.cpp` | test tool: renders an HTML file to PNG |
| `tests/` | test pages and `run-tests.sh` |

## Building

Built by Mulberry's normal build when the HTML viewer is enabled (the
default; `configure --disable-html-viewer` or `scripts/build-freebsd.sh -n`
leave it out). `Libraries/Makefile` first builds pixman, Cairo, HarfBuzz and
litehtml from their submodules into `Libraries/htmldeps` (static libraries),
then this directory: `libHTMLView.a` and `html2png`.

To rebuild only this directory: `gmake -C Libraries/HTMLView`.

## html2png

```
Libraries/HTMLView/html2png [options] input.html [output.png]

  -w WIDTH    layout width in pixels (default 800)
  -f FAMILY   default font family (default sans-serif)
  -s SIZE     default font size in pixels (default 16)
  -c CHARSET  charset of the input (default: from a byte order mark or
              <meta charset>, else UTF-8)
  -m HEIGHT   maximum height of the image (default 32000)
  -e COUNT    maximum number of elements (default: the viewer's, 20000)
  -R DIR      remote images from files: after the first layout, each remote
              image URL is given the file of DIR named as the URL's last
              path segment, as if downloaded (no network)
  -t          print timings and memory use
```

The image is exactly what the viewer draws. Images: `data:` URLs, and
`cid:NAME` or relative names as files `NAME` next to the input file (Mulberry
finds them among the mail's parts instead). Remote images (http, https, ...)
are never loaded from the network: with `-R DIR` they are taken from files,
as if downloaded; `-t` reports how many of the distinct remote URLs were
loaded and whether a new layout was needed.

To test a mail: save its HTML part to a file (in Mulberry, the part's
"Extract" in the parts list) and render it. Mulberry will pass the part to
the viewer already decoded, as UTF-8; for a saved part, give its charset with
`-c` if it has no `<meta charset>`.

Exit status: 0 rendered, 1 error, 2 usage, 3 rejected by the viewer's limits.

## Tests

`gmake -C Libraries/HTMLView check` renders `tests/*.html` and generated
stress pages into `tests/out` (one PNG and one timing line per page). It
fails if a page crashes or hangs; the PNG files are for looking at.

The samples imitating mail clients (Outlook, Gmail, Thunderbird, Apple Mail,
a newsletter) are synthetic. `40-…` and `41-…` are real newsletters,
anonymised (recipient address, customer number, all links and image URLs
replaced). `tests/images` holds generated images for `50-images.html`.
Real mail kept for local testing (`tests/*.eml`) is ignored by git.

## Images

| Source | Shown |
|---|---|
| `data:` URL (base64 or percent-encoded) | yes, decoded by the viewer |
| `cid:` (RFC 2392) | yes: the mail part with that Content-ID (Mulberry) |
| relative name (`image001.png`) | yes: the mail part with that file name (Mulberry) |
| `http:`, `https:`, ... | not by the viewer: listed (`Document::GetRemoteImageURLs()`); the host downloads them and gives them with `SetRemoteImage()`, which says whether a redraw or a new layout is needed |

Formats: PNG (Cairo), JPEG including CMYK (libjpeg-turbo), GIF first frame
(giflib), WebP still images (libwebp, if found by configure). `<img>`,
CSS `background-image` and the `background` attribute.

## Limits

Checked by `Document::Load()` before litehtml parses the HTML; a rejected
document is shown by Mulberry as text, as today:

| Limit | Default | Why |
|---|---|---|
| `max_html_size` | 16 MB | memory |
| `max_nesting_depth` | 512 | litehtml is recursive (stack overflow); its parser, Gumbo, takes quadratic time in the depth |
| `max_elements` | 20,000 | litehtml uses about 12 KB and 30–60 µs per element |
| `max_image_bytes` | 16 MB | per image, encoded |
| `max_image_pixels` | 40 million | per image, decoded (decompression bombs) |

## Known limitations (litehtml)

- `letter-spacing` and `word-spacing` are ignored.
- No bidirectional text: each right-to-left word (Arabic, Hebrew) is shaped
  and joined correctly, but the words of a line stay in source order from
  left to right, also in purely right-to-left text, and `dir="rtl"` is
  ignored. This needs a change in litehtml itself (the container only
  draws the words where litehtml puts them).
- `cellpadding` and `<ol type>` are not mapped by litehtml itself; the
  viewer adds style rules for them (`src/Document.cpp`).
