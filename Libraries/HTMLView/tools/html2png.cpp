/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// html2png: renders an HTML file to PNG with Mulberry's HTML viewer core
// (htmlview::Document), exactly as the viewer lays it out and draws it.
// A test tool: see README.md.

#include "htmlview/Document.h"
#include "htmlview/Fetcher.h"

#include <cairo.h>

#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <strings.h>

namespace
{

const char* const cUsage =
	"usage: html2png [options] input.html [output.png]\n"
	"\n"
	"Renders an HTML file to PNG with Mulberry's HTML viewer core.\n"
	"\n"
	"  -w WIDTH    layout width in pixels (default 800)\n"
	"  -f FAMILY   default font family (default sans-serif)\n"
	"  -s SIZE     default font size in pixels (default 16)\n"
	"  -c CHARSET  charset of the input (default: from a byte order mark or\n"
	"              <meta charset>, else UTF-8)\n"
	"  -m HEIGHT   maximum height of the image (default 32000); longer\n"
	"              documents are cut\n"
	"  -e COUNT    maximum number of elements (default: the viewer's, 20000)\n"
	"  -R DIR      remote images from files: after the first layout, each\n"
	"              remote image URL is given the file of DIR named as the\n"
	"              URL's last path segment, as if downloaded (no network)\n"
	"  -L          download the remote images (http, https) after the first\n"
	"              layout, as Mulberry's \"Load Remote Images\" does; failures\n"
	"              are reported on stderr\n"
	"  -T SECONDS  time limit per remote image (default 30)\n"
	"  -V          print the network log (libcurl's messages and headers) on\n"
	"              stderr, as Mulberry writes it to its HTTP log\n"
	"  -t          print timings and memory use\n"
	"  -h          show this help\n"
	"\n"
	"The output defaults to the input's name with .png.\n"
	"\n"
	"Images: data: URLs, and \"cid:NAME\" or relative names as files NAME in\n"
	"the input's directory (as Mulberry finds them among the mail's parts).\n"
	"Remote images (http, https, ...) are loaded only with -L.\n"
	"\n"
	"Exit status: 0 rendered, 1 error, 2 usage, 3 document rejected by the\n"
	"viewer's limits (too large, too many elements or nested too deeply).\n";

// Remote images downloaded with -L
class DownloadHost : public htmlview::FetchHost
{
public:
	DownloadHost(htmlview::Document& document, bool log) : mDocument(document), mLog(log) {}

	void FetchDone(const std::string& url, bool ok, const std::string& data, const std::string& error) override
	{
		if (!ok)
		{
			std::cerr << "html2png: remote image failed: " << url << ": " << error << "\n";
			return;
		}
		htmlview::Document::ImageChange change = mDocument.SetRemoteImage(url, data);
		if (change == htmlview::Document::ImageChange::Relayout)
			mRelayout = true;
		else if (change == htmlview::Document::ImageChange::NoChange)
			std::cerr << "html2png: remote image not shown (format or size): " << url << "\n";
	}

	bool LogEnabled() const override { return mLog; }
	void Log(const std::string& text) override { std::cerr << text << "\n"; }

	bool mRelayout = false;

private:
	htmlview::Document&	mDocument;
	bool				mLog;
};

// Images of the test pages: files next to the input file
class FileHost : public htmlview::Host
{
public:
	explicit FileHost(const std::string& dir) : mDir(dir) {}

	bool LoadImageData(const std::string& url, std::string& data) override
	{
		std::string name = url;
		if ((name.size() > 4) && (strncasecmp(name.c_str(), "cid:", 4) == 0))
			name.erase(0, 4);
		// Only plain names below the input's directory
		if (name.empty() || (name[0] == '/') || (name.find("..") != std::string::npos))
			return false;
		std::ifstream file(mDir + "/" + name, std::ios::binary);
		if (!file)
			return false;
		std::stringstream contents;
		contents << file.rdbuf();
		data = contents.str();
		return true;
	}

private:
	std::string mDir;
};

double Milliseconds(std::chrono::steady_clock::time_point start)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

double MaxRSSMegabytes()
{
	struct rusage usage;
	getrusage(RUSAGE_SELF, &usage);
	return usage.ru_maxrss / 1024.0;		// kilobytes on FreeBSD and Linux
}

int ParsePositive(const char* arg, const char* option, long max = 100000)
{
	char* end = nullptr;
	long value = std::strtol(arg, &end, 10);
	if (!end || *end || (value <= 0) || (value > max))
	{
		std::cerr << "html2png: invalid value for " << option << ": " << arg << "\n";
		std::exit(2);
	}
	return (int) value;
}

}	// namespace

int main(int argc, char** argv)
{
	htmlview::Settings settings;
	int width = 800;
	int max_height = 32000;		// Cairo image surfaces are limited to 32767
	std::string charset;
	std::string remote_dir;
	bool download = false;
	bool network_log = false;
	htmlview::FetchSettings fetch_settings;
	bool timings = false;

	int opt;
	while ((opt = getopt(argc, argv, "w:f:s:c:m:e:R:LT:Vth")) != -1)
	{
		if (opt == 'R')
		{
			remote_dir = optarg;
			continue;
		}
		if (opt == 'L')
		{
			download = true;
			continue;
		}
		if (opt == 'T')
		{
			fetch_settings.timeout = ParsePositive(optarg, "-T", 3600);
			continue;
		}
		if (opt == 'V')
		{
			network_log = true;
			continue;
		}
		switch (opt)
		{
		case 'w':
			width = ParsePositive(optarg, "-w");
			break;
		case 'f':
			settings.default_font_family = optarg;
			break;
		case 's':
			settings.default_font_size = ParsePositive(optarg, "-s");
			break;
		case 'c':
			charset = optarg;
			break;
		case 'm':
			max_height = std::min(ParsePositive(optarg, "-m"), 32000);
			break;
		case 'e':
			settings.max_elements = ParsePositive(optarg, "-e", 10000000);
			break;
		case 't':
			timings = true;
			break;
		case 'h':
			std::cout << cUsage;
			return 0;
		default:
			std::cerr << cUsage;
			return 2;
		}
	}
	if ((argc - optind < 1) || (argc - optind > 2))
	{
		std::cerr << cUsage;
		return 2;
	}

	std::string input = argv[optind];
	std::string output;
	if (argc - optind == 2)
		output = argv[optind + 1];
	else
	{
		output = input;
		size_t dot = output.find_last_of('.');
		size_t slash = output.find_last_of('/');
		if ((dot != std::string::npos) && ((slash == std::string::npos) || (dot > slash)))
			output.erase(dot);
		output += ".png";
	}

	std::ifstream file(input, std::ios::binary);
	if (!file)
	{
		std::cerr << "html2png: cannot read " << input << "\n";
		return 1;
	}
	std::stringstream contents;
	contents << file.rdbuf();
	std::string html = contents.str();

	auto start = std::chrono::steady_clock::now();
	size_t slash = input.find_last_of('/');
	FileHost host((slash == std::string::npos) ? std::string(".") : input.substr(0, slash));
	htmlview::Document document(settings, &host);
	htmlview::Document::Result status = document.Load(html, charset);
	double parse_ms = Milliseconds(start);
	if (status != htmlview::Document::Result::Ok)
	{
		std::cerr << "html2png: " << input << ": " << htmlview::Document::ResultText(status) << "\n";
		return (status == htmlview::Document::Result::Error) ? 1 : 3;
	}

	start = std::chrono::steady_clock::now();
	int height = document.Layout(width);
	double layout_ms = Milliseconds(start);

	// Remote images "arriving" after the first layout, as in Mulberry
	double remote_ms = 0;
	bool layout = false;
	if (!remote_dir.empty())
	{
		start = std::chrono::steady_clock::now();
		for (const std::string& url : document.GetRemoteImageURLs())
		{
			std::string name = url.substr(0, url.find_first_of("?#"));
			size_t slash = name.find_last_of('/');
			if (slash != std::string::npos)
				name.erase(0, slash + 1);
			if (name.empty() || (name == "..") || (name == "."))
				continue;
			std::ifstream file(remote_dir + "/" + name, std::ios::binary);
			if (!file)
				continue;
			std::stringstream data;
			data << file.rdbuf();
			if (document.SetRemoteImage(url, data.str()) == htmlview::Document::ImageChange::Relayout)
				layout = true;
		}
		if (layout)
			height = document.Layout(width);
		remote_ms = Milliseconds(start);
	}

	// Remote images downloaded, as in Mulberry
	if (download)
	{
		start = std::chrono::steady_clock::now();
		DownloadHost host(document, network_log);
		htmlview::Fetcher fetcher(&host, fetch_settings);
		for (const std::string& url : document.GetRemoteImageURLs())
			fetcher.Add(url);
		while (fetcher.Pump())
			fetcher.Wait(200);
		if (host.mRelayout)
		{
			layout = true;
			height = document.Layout(width);
		}
		remote_ms += Milliseconds(start);
	}

	int image_width = std::min(std::max(width, document.GetWidth()), 16000);
	int image_height = std::max(1, std::min(height, max_height));
	if (height > max_height)
		std::cerr << "html2png: document is " << height << " pixels high, image cut at " << max_height << "\n";

	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, image_width, image_height);
	cairo_t* cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);

	start = std::chrono::steady_clock::now();
	htmlview::Rect clip;
	clip.width = image_width;
	clip.height = image_height;
	document.Draw(cr, 0, 0, clip);
	cairo_surface_flush(surface);
	double draw_ms = Milliseconds(start);

	cairo_status_t written = cairo_surface_write_to_png(surface, output.c_str());
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	if (written != CAIRO_STATUS_SUCCESS)
	{
		std::cerr << "html2png: cannot write " << output << ": " << cairo_status_to_string(written) << "\n";
		return 1;
	}

	if (timings)
	{
		char line[256];
		std::snprintf(line, sizeof(line),
					  "parse %.1f ms, layout %.1f ms, draw %.1f ms; %dx%d px; max RSS %.1f MB",
					  parse_ms, layout_ms, draw_ms, document.GetWidth(), height, MaxRSSMegabytes());
		size_t remote = document.GetRemoteImageURLs().size();
		if (remote > 0)
			std::snprintf(line + std::strlen(line), sizeof(line) - std::strlen(line),
						  "; remote images: %d of %zu loaded (%.1f ms%s)", document.GetLoadedRemoteImages(),
						  remote, remote_ms, layout ? ", new layout" : "");
		std::cout << line << "\n";
	}
	return 0;
}
