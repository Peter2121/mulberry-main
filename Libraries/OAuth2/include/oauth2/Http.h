/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Settings of the library's HTTP requests (token endpoint; later device
// authorization and revocation).  The requests are made with libcurl:
// HTTPS only, certificate and host name verified, no redirects followed, no
// cookies, no .netrc, size and time limits.

#ifndef OAUTH2_HTTP_H
#define OAUTH2_HTTP_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

namespace oauth2
{

// Receives the log, one line per call, in the style of "curl -v": "* " for
// information, "> " for what is sent, "< " for what is received.  Secrets
// are already replaced by "<redacted N bytes>" (Redact.h): form fields,
// JSON fields, Authorization and cookie headers.
typedef std::function<void(const std::string& line)> LogFunction;

struct HttpSettings
{
	// Neutral by default, like the image fetcher's (OAUTH2-PLAN.md, section
	// 3: application identity)
	std::string user_agent = "Mozilla/5.0";

	long connect_timeout = 15;					// seconds
	long timeout = 30;							// seconds, whole request
	size_t max_response_bytes = 1024 * 1024;	// larger answers are refused

	// Tests only: also accept http:// when the host is 127.0.0.1, [::1] or
	// localhost (local mock server).  Proxies are never used for these.
	bool allow_http_loopback = false;

	// Certificate authorities (PEM file) instead of the system's, for a
	// local test server or a company proxy; empty: the system's
	std::string ca_file;

	LogFunction log;							// none: no log

	// Set to true by another thread to stop the request (reported as
	// cancelled)
	const std::atomic<bool>* cancel = nullptr;
};

} // namespace oauth2

#endif
