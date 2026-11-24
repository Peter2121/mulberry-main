/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Images for the HTML viewer: decoding (PNG through Cairo, JPEG with
// libjpeg, GIF with giflib, WebP with libwebp when built with it) and data:
// URLs

#ifndef HTMLVIEW_IMAGES_H
#define HTMLVIEW_IMAGES_H

#include <cairo.h>

#include <cstddef>
#include <string>

namespace htmlview
{

// Where an image URL points to
enum class URLKind
{
	Data,		// data: (inside the document)
	Local,		// cid:, mid:, or a name without a scheme: found by the host (in the mail)
	Remote		// any other scheme (http, https, ftp, file, ...): never loaded here
};

URLKind ClassifyURL(const std::string& url);

// The bytes of a data: URL (base64 or percent-encoded); false if it is not a
// valid data: URL or decodes to more than max_bytes
bool DecodeDataURL(const std::string& url, size_t max_bytes, std::string& data);

// Decodes an image (format from its first bytes) into a Cairo image surface,
// premultiplied ARGB32 (or RGB24 when opaque).  GIF: the first frame.
// nullptr if the format is unknown, the data broken, or the image larger
// than max_pixels.
cairo_surface_t* DecodeImage(const std::string& data, size_t max_pixels);

// Whether DecodeImage() can be expected to succeed: a known format whose
// header gives a size within max_pixels (the pixels are not decoded)
bool ProbeImage(const std::string& data, size_t max_pixels);

}	// namespace htmlview

#endif
