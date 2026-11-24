/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// htmlview::Fetcher: downloads the remote images of a message (http, https)
// with libcurl, without blocking and without threads: the host calls Pump()
// regularly (from an idle task or a timer) and receives each image through
// FetchHost::FetchDone().  It knows nothing about documents: the host gives
// the data to htmlview::Document::SetRemoteImage().
//
// Every request: http and https only (also after redirects, at most
// max_redirects); no cookies, no Referer, no credentials (a user name in the
// URL is refused, .netrc is not read); a neutral User-Agent; Accept for the
// image formats the viewer decodes; size and time limits.  TLS certificates
// are checked as usual: a failure is reported as an error, never asked about.
// No header of the libcurl API is needed to use this one.

#ifndef HTMLVIEW_FETCHER_H
#define HTMLVIEW_FETCHER_H

#include <cstddef>
#include <memory>
#include <string>

namespace htmlview
{

// TODO(html-viewer-preferences): limits and time-outs may become preferences
// (LITEHTML-VIEWER-PLAN.md, section 11.13)
struct FetchSettings
{
	size_t		max_bytes = 16 * 1024 * 1024;			// per image (as the viewer's max_image_bytes)
	size_t		max_total_bytes = 50 * 1024 * 1024;		// all images of one Fetcher
	size_t		max_requests = 200;						// images of one Fetcher
	long		connect_timeout = 10;					// seconds
	long		timeout = 30;							// seconds, per image
	long		max_redirects = 5;
	long		max_host_connections = 6;				// as browsers
	long		max_connections = 12;
	std::string	user_agent = "Mozilla/5.0";
};

// Callbacks of a Fetcher, all called from Pump() (or from Add() for a URL
// refused at once)
class FetchHost
{
public:
	virtual ~FetchHost() = default;

	// An image finished: ok with its data, or failed with an error text.  The
	// Fetcher must not be destroyed from here (Cancel() is allowed).
	virtual void FetchDone(const std::string& url, bool ok, const std::string& data,
						   const std::string& error) = 0;

	// Network log (e.g. Mulberry's HTTP log): LogEnabled() is asked when a
	// download starts; Log() then receives libcurl's messages and the
	// request and response headers (never the image data).
	virtual bool LogEnabled() const { return false; }
	virtual void Log(const std::string& text) {}
};

class Fetcher
{
public:
	explicit Fetcher(FetchHost* host, const FetchSettings& settings = FetchSettings());
	~Fetcher();			// cancels what is still running

	Fetcher(const Fetcher&) = delete;
	Fetcher& operator=(const Fetcher&) = delete;

	// Starts downloading an image.  A URL that is not http or https, or one
	// over the max_requests limit, fails at once (FetchDone()).  The same
	// URL twice: once.
	void	Add(const std::string& url);

	// Does what can be done now without waiting, and reports the images that
	// finished.  Returns true while downloads are still running.
	bool	Pump();

	// Waits until there is network activity or at most timeout_ms (for tools
	// without an event loop; Mulberry pumps from an idle task instead)
	void	Wait(int timeout_ms);

	// Stops all downloads; they are not reported
	void	Cancel();

	size_t	GetRunning() const;		// downloads not finished
	size_t	GetStarted() const;		// downloads started (Add() accepted)

private:
	struct Impl;
	std::unique_ptr<Impl> mImpl;
};

}	// namespace htmlview

#endif
