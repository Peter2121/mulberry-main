/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Unit test of htmlview::ImageCache (run by "gmake check")

#include "htmlview/ImageCache.h"

#include <iostream>
#include <string>

namespace
{

int sFailed = 0;

void Check(bool ok, const char* what)
{
	std::cout << (ok ? "ok      " : "FAILED  ") << what << "\n";
	if (!ok)
		sFailed++;
}

// An entry of n bytes in the cache's accounting (URL + data)
std::string Data(const std::string& url, size_t n)
{
	return std::string(n - url.size(), 'x');
}

}	// namespace

int main()
{
	using htmlview::ImageCache;
	std::string data;

	Check(ImageCache::cDefaultMaxBytes == 128 * 1024 * 1024, "default size 128 MB");

	// Room for three entries of 100 bytes
	ImageCache cache(300);
	cache.Put("a", Data("a", 100));
	cache.Put("b", Data("b", 100));
	cache.Put("c", Data("c", 100));
	Check((cache.GetCount() == 3) && (cache.GetBytes() == 300), "three images stored, 300 bytes");
	Check(cache.Get("a", data) && (data == Data("a", 100)), "an image is found with its data");
	Check(!cache.Get("z", data), "an unknown URL is not found");

	// "a" was just used: "b" is now the least recently used
	cache.Put("d", Data("d", 100));
	Check(!cache.Contains("b") && cache.Contains("a") && cache.Contains("c") && cache.Contains("d"),
		  "full: the least recently used image goes (not the one just looked up)");

	// Contains() does not change the order: "c" stays the oldest
	cache.Contains("c");
	cache.Put("e", Data("e", 100));
	Check(!cache.Contains("c") && cache.Contains("a"), "Contains() does not count as a use");

	// Replacing an image updates the size and makes it the most recent
	cache.Put("a", Data("a", 50));
	Check((cache.GetCount() == 3) && (cache.GetBytes() == 250), "replacing an image updates the size");
	cache.Put("f", Data("f", 100));
	Check(cache.Contains("a") && !cache.Contains("d"), "a replaced image is the most recently used");

	// Larger than everything that is there: older images go
	cache.Put("g", Data("g", 250));
	Check(cache.Contains("g") && (cache.GetCount() == 1) && (cache.GetBytes() == 250),
		  "a large image removes as many older ones as needed");

	// Larger than the whole cache: not stored, nothing removed
	cache.Put("h", Data("h", 301));
	Check(!cache.Contains("h") && cache.Contains("g"), "an image larger than the cache is not stored");

	// A smaller size removes images at once
	cache.SetMaxBytes(1000);
	cache.Put("i", Data("i", 100));
	cache.Put("j", Data("j", 100));
	cache.SetMaxBytes(200);
	Check((cache.GetBytes() <= 200) && cache.Contains("j") && cache.Contains("i") && !cache.Contains("g"),
		  "a smaller size removes the least recently used images");

	cache.Clear();
	Check((cache.GetCount() == 0) && (cache.GetBytes() == 0) && !cache.Contains("j"), "Clear() empties the cache");

	// Binary data (with zero bytes) is kept as it is
	std::string binary("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16);
	cache.Put("png", binary);
	Check(cache.Get("png", data) && (data == binary) && (data.size() == 16), "binary data kept as it is");

	std::cout << "\n" << (sFailed ? "image cache: some checks failed" : "image cache: all checks passed") << "\n";
	return sFailed ? 1 : 0;
}
