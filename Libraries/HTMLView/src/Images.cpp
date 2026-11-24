/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "Images.h"

#include <cctype>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

// libjpeg's header needs stdio's FILE and size_t first
#include <jpeglib.h>
#include <gif_lib.h>
#ifdef HTMLVIEW_WEBP
#include <webp/decode.h>
#endif

namespace htmlview
{

namespace
{

// Cairo's limit for image surfaces
const int cMaxSide = 32767;

bool TooLarge(long width, long height, size_t max_pixels)
{
	return (width <= 0) || (height <= 0) || (width > cMaxSide) || (height > cMaxSide) ||
		   ((size_t) width * (size_t) height > max_pixels);
}

bool StartsWith(const std::string& data, const char* magic, size_t offset = 0)
{
	size_t len = std::strlen(magic);
	return (data.size() >= offset + len) && (std::memcmp(data.data() + offset, magic, len) == 0);
}

inline uint32_t OpaquePixel(unsigned r, unsigned g, unsigned b)
{
	return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// ---------------------------------------------------------------------------
// PNG (Cairo's own reader)
// ---------------------------------------------------------------------------

struct MemoryReader
{
	const unsigned char*	data;
	size_t					left;
};

cairo_status_t ReadPNGData(void* closure, unsigned char* buffer, unsigned int length)
{
	auto* reader = static_cast<MemoryReader*>(closure);
	if (length > reader->left)
		return CAIRO_STATUS_READ_ERROR;
	std::memcpy(buffer, reader->data, length);
	reader->data += length;
	reader->left -= length;
	return CAIRO_STATUS_SUCCESS;
}

cairo_surface_t* DecodePNG(const std::string& data, size_t max_pixels)
{
	// The size is in the IHDR chunk, right after the signature
	if (data.size() < 24)
		return nullptr;
	const unsigned char* p = (const unsigned char*) data.data();
	long width = ((long) p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19];
	long height = ((long) p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23];
	if (TooLarge(width, height, max_pixels))
		return nullptr;

	MemoryReader reader = { p, data.size() };
	cairo_surface_t* surface = cairo_image_surface_create_from_png_stream(ReadPNGData, &reader);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
	{
		cairo_surface_destroy(surface);
		return nullptr;
	}
	return surface;
}

// ---------------------------------------------------------------------------
// JPEG (libjpeg, libjpeg-turbo)
// ---------------------------------------------------------------------------

struct JPEGError
{
	jpeg_error_mgr	mgr;
	jmp_buf			jump;
};

void JPEGErrorExit(j_common_ptr cinfo)
{
	longjmp(reinterpret_cast<JPEGError*>(cinfo->err)->jump, 1);
}

void JPEGOutputMessage(j_common_ptr)
{
	// Warnings about slightly broken files are not printed
}

// Everything that must be released if libjpeg gives up (longjmp: no
// destructors run, so only plain pointers)
struct JPEGState
{
	jpeg_decompress_struct	cinfo;
	JPEGError				error;
	cairo_surface_t*		surface = nullptr;
	unsigned char*			row = nullptr;
};

cairo_surface_t* DecodeJPEG(const std::string& data, size_t max_pixels)
{
	JPEGState* state = new JPEGState;
	state->cinfo.err = jpeg_std_error(&state->error.mgr);
	state->error.mgr.error_exit = JPEGErrorExit;
	state->error.mgr.output_message = JPEGOutputMessage;

	if (setjmp(state->error.jump))
	{
		jpeg_destroy_decompress(&state->cinfo);
		if (state->surface)
			cairo_surface_destroy(state->surface);
		std::free(state->row);
		delete state;
		return nullptr;
	}

	jpeg_create_decompress(&state->cinfo);
	jpeg_mem_src(&state->cinfo, (const unsigned char*) data.data(), (unsigned long) data.size());
	jpeg_read_header(&state->cinfo, TRUE);

	long width = state->cinfo.image_width;
	long height = state->cinfo.image_height;
	if (TooLarge(width, height, max_pixels))
		longjmp(state->error.jump, 1);

	// CMYK (Adobe Photoshop's JPEGs) is converted by hand; the rest straight
	// into Cairo's pixel layout (a 32-bit word: 0xAARRGGBB)
	bool cmyk = (state->cinfo.jpeg_color_space == JCS_CMYK) || (state->cinfo.jpeg_color_space == JCS_YCCK);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	state->cinfo.out_color_space = cmyk ? JCS_CMYK : JCS_EXT_BGRA;
#else
	state->cinfo.out_color_space = cmyk ? JCS_CMYK : JCS_EXT_ARGB;
#endif
	jpeg_start_decompress(&state->cinfo);

	width = state->cinfo.output_width;
	height = state->cinfo.output_height;
	state->surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, (int) width, (int) height);
	if (cairo_surface_status(state->surface) != CAIRO_STATUS_SUCCESS)
		longjmp(state->error.jump, 1);
	cairo_surface_flush(state->surface);
	unsigned char* pixels = cairo_image_surface_get_data(state->surface);
	int stride = cairo_image_surface_get_stride(state->surface);
	if (cmyk)
		state->row = (unsigned char*) std::malloc((size_t) width * 4);

	// Adobe writes inverted CMYK
	bool inverted = state->cinfo.saw_Adobe_marker;
	while (state->cinfo.output_scanline < state->cinfo.output_height)
	{
		unsigned char* line = pixels + (size_t) state->cinfo.output_scanline * stride;
		JSAMPROW rows[1] = { cmyk ? state->row : line };
		if (jpeg_read_scanlines(&state->cinfo, rows, 1) != 1)
			break;
		if (cmyk)
		{
			uint32_t* out = (uint32_t*) line;
			const unsigned char* in = state->row;
			for (long x = 0; x < width; x++, in += 4)
			{
				unsigned c = inverted ? in[0] : 255 - in[0];
				unsigned m = inverted ? in[1] : 255 - in[1];
				unsigned y = inverted ? in[2] : 255 - in[2];
				unsigned k = inverted ? in[3] : 255 - in[3];
				out[x] = OpaquePixel(c * k / 255, m * k / 255, y * k / 255);
			}
		}
	}

	jpeg_finish_decompress(&state->cinfo);
	jpeg_destroy_decompress(&state->cinfo);
	cairo_surface_t* surface = state->surface;
	std::free(state->row);
	delete state;

	cairo_surface_mark_dirty(surface);
	return surface;
}

// The size in a JPEG's header (libjpeg reads only the markers)
bool ProbeJPEG(const std::string& data, size_t max_pixels)
{
	JPEGState* state = new JPEGState;
	state->cinfo.err = jpeg_std_error(&state->error.mgr);
	state->error.mgr.error_exit = JPEGErrorExit;
	state->error.mgr.output_message = JPEGOutputMessage;
	if (setjmp(state->error.jump))
	{
		jpeg_destroy_decompress(&state->cinfo);
		delete state;
		return false;
	}
	jpeg_create_decompress(&state->cinfo);
	jpeg_mem_src(&state->cinfo, (const unsigned char*) data.data(), (unsigned long) data.size());
	jpeg_read_header(&state->cinfo, TRUE);
	bool ok = !TooLarge(state->cinfo.image_width, state->cinfo.image_height, max_pixels);
	jpeg_destroy_decompress(&state->cinfo);
	delete state;
	return ok;
}

// ---------------------------------------------------------------------------
// GIF (giflib): the first frame, on the logical screen
// ---------------------------------------------------------------------------

int ReadGIFData(GifFileType* gif, GifByteType* buffer, int length)
{
	auto* reader = static_cast<MemoryReader*>(gif->UserData);
	size_t n = ((size_t) length > reader->left) ? reader->left : (size_t) length;
	std::memcpy(buffer, reader->data, n);
	reader->data += n;
	reader->left -= n;
	return (int) n;
}

cairo_surface_t* DecodeGIF(const std::string& data, size_t max_pixels)
{
	MemoryReader reader = { (const unsigned char*) data.data(), data.size() };
	int error = 0;
	GifFileType* gif = DGifOpen(&reader, ReadGIFData, &error);
	if (!gif)
		return nullptr;

	// The screen size first: DGifSlurp() would allocate the frames anyway
	long width = gif->SWidth;
	long height = gif->SHeight;
	cairo_surface_t* surface = nullptr;
	if (!TooLarge(width, height, max_pixels) && (DGifSlurp(gif) == GIF_OK) && (gif->ImageCount > 0))
	{
		const SavedImage& frame = gif->SavedImages[0];
		const ColorMapObject* colors = frame.ImageDesc.ColorMap ? frame.ImageDesc.ColorMap : gif->SColorMap;
		GraphicsControlBlock control;
		int transparent = -1;
		if (DGifSavedExtensionToGCB(gif, 0, &control) == GIF_OK)
			transparent = control.TransparentColor;

		if (colors && frame.RasterBits)
		{
			surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int) width, (int) height);
			if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS)
			{
				// A new surface is transparent: draw the frame's pixels on it
				cairo_surface_flush(surface);
				unsigned char* pixels = cairo_image_surface_get_data(surface);
				int stride = cairo_image_surface_get_stride(surface);
				const GifImageDesc& desc = frame.ImageDesc;
				for (int row = 0; row < desc.Height; row++)
				{
					long y = desc.Top + row;
					if ((y < 0) || (y >= height))
						continue;
					uint32_t* out = (uint32_t*) (pixels + (size_t) y * stride);
					const GifByteType* in = frame.RasterBits + (size_t) row * desc.Width;
					for (int col = 0; col < desc.Width; col++)
					{
						long x = desc.Left + col;
						int index = in[col];
						if ((x < 0) || (x >= width) || (index == transparent) || (index >= colors->ColorCount))
							continue;
						const GifColorType& c = colors->Colors[index];
						out[x] = OpaquePixel(c.Red, c.Green, c.Blue);
					}
				}
				cairo_surface_mark_dirty(surface);
			}
			else
			{
				cairo_surface_destroy(surface);
				surface = nullptr;
			}
		}
	}

	DGifCloseFile(gif, &error);
	return surface;
}

// ---------------------------------------------------------------------------
// WebP (libwebp), still images
// ---------------------------------------------------------------------------

#ifdef HTMLVIEW_WEBP
cairo_surface_t* DecodeWebP(const std::string& data, size_t max_pixels)
{
	const uint8_t* bytes = (const uint8_t*) data.data();
	int width = 0;
	int height = 0;
	if (!WebPGetInfo(bytes, data.size(), &width, &height) || TooLarge(width, height, max_pixels))
		return nullptr;

	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
	{
		cairo_surface_destroy(surface);
		return nullptr;
	}
	cairo_surface_flush(surface);
	unsigned char* pixels = cairo_image_surface_get_data(surface);
	int stride = cairo_image_surface_get_stride(surface);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint8_t* done = WebPDecodeBGRAInto(bytes, data.size(), pixels, (size_t) stride * height, stride);
#else
	uint8_t* done = WebPDecodeARGBInto(bytes, data.size(), pixels, (size_t) stride * height, stride);
#endif
	if (!done)
	{
		cairo_surface_destroy(surface);
		return nullptr;
	}

	// Cairo wants premultiplied alpha
	for (int y = 0; y < height; y++)
	{
		uint32_t* row = (uint32_t*) (pixels + (size_t) y * stride);
		for (int x = 0; x < width; x++)
		{
			uint32_t p = row[x];
			uint32_t a = p >> 24;
			if (a == 255)
				continue;
			uint32_t r = ((p >> 16) & 0xFF) * a / 255;
			uint32_t g = ((p >> 8) & 0xFF) * a / 255;
			uint32_t b = (p & 0xFF) * a / 255;
			row[x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}
	cairo_surface_mark_dirty(surface);
	return surface;
}
#endif

// ---------------------------------------------------------------------------
// URLs
// ---------------------------------------------------------------------------

int HexValue(char c)
{
	if ((c >= '0') && (c <= '9'))
		return c - '0';
	if ((c >= 'a') && (c <= 'f'))
		return c - 'a' + 10;
	if ((c >= 'A') && (c <= 'F'))
		return c - 'A' + 10;
	return -1;
}

std::string PercentDecode(const std::string& text)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++)
	{
		if ((text[i] == '%') && (i + 2 < text.size()))
		{
			int high = HexValue(text[i + 1]);
			int low = HexValue(text[i + 2]);
			if ((high >= 0) && (low >= 0))
			{
				out += (char) ((high << 4) | low);
				i += 2;
				continue;
			}
		}
		out += text[i];
	}
	return out;
}

int Base64Value(char c)
{
	if ((c >= 'A') && (c <= 'Z'))
		return c - 'A';
	if ((c >= 'a') && (c <= 'z'))
		return c - 'a' + 26;
	if ((c >= '0') && (c <= '9'))
		return c - '0' + 52;
	if ((c == '+') || (c == '-'))
		return 62;
	if ((c == '/') || (c == '_'))
		return 63;
	return -1;
}

}	// namespace

URLKind ClassifyURL(const std::string& url)
{
	// A scheme: a letter, then letters, digits, "+", "-" or ".", then ":"
	size_t i = 0;
	while ((i < url.size()) && std::isspace((unsigned char) url[i]))
		i++;
	size_t start = i;
	if ((i < url.size()) && std::isalpha((unsigned char) url[i]))
	{
		while ((i < url.size()) &&
			   (std::isalnum((unsigned char) url[i]) || (url[i] == '+') || (url[i] == '-') || (url[i] == '.')))
			i++;
		if ((i < url.size()) && (url[i] == ':'))
		{
			std::string scheme;
			for (size_t k = start; k < i; k++)
				scheme += (char) std::tolower((unsigned char) url[k]);
			if (scheme == "data")
				return URLKind::Data;
			if ((scheme == "cid") || (scheme == "mid"))
				return URLKind::Local;
			return URLKind::Remote;
		}
	}
	return URLKind::Local;
}

bool DecodeDataURL(const std::string& url, size_t max_bytes, std::string& data)
{
	size_t start = 0;
	while ((start < url.size()) && std::isspace((unsigned char) url[start]))
		start++;
	if ((url.size() < start + 5) || (strncasecmp(url.c_str() + start, "data:", 5) != 0))
		return false;
	size_t comma = url.find(',', start + 5);
	if (comma == std::string::npos)
		return false;

	std::string header = url.substr(start + 5, comma - start - 5);
	bool base64 = (header.size() >= 7) && (strncasecmp(header.c_str() + header.size() - 7, ";base64", 7) == 0);
	std::string payload = PercentDecode(url.substr(comma + 1));

	data.clear();
	if (!base64)
	{
		if (payload.size() > max_bytes)
			return false;
		data = payload;
		return true;
	}

	data.reserve(payload.size() * 3 / 4);
	unsigned buffer = 0;
	int bits = 0;
	for (char c : payload)
	{
		int value = Base64Value(c);
		if (value < 0)
			continue;		// whitespace, "=" padding
		buffer = (buffer << 6) | (unsigned) value;
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			data += (char) ((buffer >> bits) & 0xFF);
			if (data.size() > max_bytes)
				return false;
		}
	}
	return true;
}

bool ProbeImage(const std::string& data, size_t max_pixels)
{
	const unsigned char* p = (const unsigned char*) data.data();
	if (StartsWith(data, "\x89PNG\r\n\x1a\n") && (data.size() >= 24))
		return !TooLarge(((long) p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19],
						 ((long) p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23], max_pixels);
	if (StartsWith(data, "\xFF\xD8\xFF"))
		return ProbeJPEG(data, max_pixels);
	if ((StartsWith(data, "GIF87a") || StartsWith(data, "GIF89a")) && (data.size() >= 10))
		return !TooLarge(p[6] | (p[7] << 8), p[8] | (p[9] << 8), max_pixels);
#ifdef HTMLVIEW_WEBP
	if (StartsWith(data, "RIFF") && StartsWith(data, "WEBP", 8))
	{
		int width = 0;
		int height = 0;
		return WebPGetInfo(p, data.size(), &width, &height) && !TooLarge(width, height, max_pixels);
	}
#endif
	return false;
}

cairo_surface_t* DecodeImage(const std::string& data, size_t max_pixels)
{
	if (StartsWith(data, "\x89PNG\r\n\x1a\n"))
		return DecodePNG(data, max_pixels);
	if (StartsWith(data, "\xFF\xD8\xFF"))
		return DecodeJPEG(data, max_pixels);
	if (StartsWith(data, "GIF87a") || StartsWith(data, "GIF89a"))
		return DecodeGIF(data, max_pixels);
#ifdef HTMLVIEW_WEBP
	if (StartsWith(data, "RIFF") && StartsWith(data, "WEBP", 8))
		return DecodeWebP(data, max_pixels);
#endif
	return nullptr;
}

}	// namespace htmlview
