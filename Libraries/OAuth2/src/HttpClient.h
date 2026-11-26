/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Private: form POST over libcurl, with the rules of Http.h

#ifndef OAUTH2_HTTPCLIENT_H
#define OAUTH2_HTTPCLIENT_H

#include "oauth2/Encoding.h"
#include "oauth2/Http.h"

#include <string>
#include <vector>

namespace oauth2
{

struct HttpResult
{
	enum class Outcome
	{
		Done,			// an HTTP answer was received (any status)
		BadUrl,			// refused before sending (error says why)
		Network,		// connection, TLS or protocol failure
		Timeout,
		TooLarge,
		Cancelled,
		RedirectRefused	// status and error say which and why
	};

	Outcome outcome = Outcome::BadUrl;
	std::string error;			// readable, for anything but Done
	long status = 0;
	std::string content_type;
	std::string body;
};

// Checks a request URL: https, or http to a loopback host when
// settings.allow_http_loopback; no user name or password in it.  loopback
// tells whether the host is 127.0.0.1, [::1] or localhost.
bool CheckRequestUrl(const std::string& url, const HttpSettings& settings, bool& loopback,
	std::string& error);

// Most redirects followed by one request
const int cMaxRedirects = 3;

// Whether a redirect from the URL from to the URL to may be followed with
// the same form: to passes CheckRequestUrl(), and its host is the host of
// from, or one of redirect_domains or a subdomain of one.  Host names are
// compared without case and without a final dot.
bool IsRedirectAllowed(const std::string& from, const std::string& to,
	const std::vector<std::string>& redirect_domains, const HttpSettings& settings,
	std::string& error);

// POSTs form as application/x-www-form-urlencoded with "Accept:
// application/json".  Redirects: 307 and 308 (which keep the method and
// the form) are followed when IsRedirectAllowed(), at most cMaxRedirects
// times; 301, 302 and 303 (which would turn the request into a GET without
// the form) are not.  A 3xx without a target is returned as Done.  The log
// gets the request and answer headers, each redirect, and the form with
// RedactParams(); the answer body is left to the caller, which knows its
// format.  Returns true when outcome is Done.
bool PostForm(const std::string& url, const Params& form,
	const std::vector<std::string>& redirect_domains, const HttpSettings& settings,
	HttpResult& result);

// Copy of a header line with the value of Authorization, Proxy-Authorization,
// Cookie and Set-Cookie replaced by Redact()
std::string RedactHeaderLine(const std::string& line);

} // namespace oauth2

#endif
