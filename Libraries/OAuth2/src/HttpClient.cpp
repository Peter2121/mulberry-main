/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "HttpClient.h"

#include "oauth2/Redact.h"

#include <curl/curl.h>

#include <cctype>
#include <mutex>

namespace oauth2
{

namespace
{

void EnsureCurl()
{
	static std::once_flag once;
	std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::string Lower(std::string text)
{
	for (char& c : text)
		c = (char) std::tolower((unsigned char) c);
	return text;
}

struct Transfer
{
	const HttpSettings*	settings = nullptr;
	std::string			body;
	bool				too_large = false;
	std::string			form_log;			// the form, redacted
	bool				form_logged = false;
};

void Log(const HttpSettings& settings, const std::string& line)
{
	if (settings.log)
		settings.log(line);
}

size_t Write(char* buffer, size_t size, size_t count, void* user)
{
	auto* transfer = static_cast<Transfer*>(user);
	const size_t bytes = size * count;
	if (transfer->body.size() + bytes > transfer->settings->max_response_bytes)
	{
		// libcurl then stops with a write error
		transfer->too_large = true;
		return 0;
	}
	transfer->body.append(buffer, bytes);
	return bytes;
}

int Progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	auto* transfer = static_cast<Transfer*>(user);
	const std::atomic<bool>* cancel = transfer->settings->cancel;
	return (cancel && cancel->load()) ? 1 : 0;
}

int Debug(CURL*, curl_infotype type, char* text, size_t size, void* user)
{
	auto* transfer = static_cast<Transfer*>(user);
	const char* prefix = nullptr;
	bool headers = false;
	switch (type)
	{
	case CURLINFO_TEXT:
		prefix = "* ";
		break;
	case CURLINFO_HEADER_OUT:
		prefix = "> ";
		headers = true;
		break;
	case CURLINFO_HEADER_IN:
		prefix = "< ";
		headers = true;
		break;
	case CURLINFO_DATA_OUT:
		// The form as built, redacted, instead of the raw bytes
		if (!transfer->form_logged)
		{
			transfer->form_logged = true;
			Log(*transfer->settings, "> " + transfer->form_log);
		}
		return 0;
	default:
		// Not the answer body (logged by the caller, redacted) nor TLS data
		return 0;
	}

	std::string line;
	for (size_t i = 0; i <= size; ++i)
	{
		const char c = (i < size) ? text[i] : '\n';
		if (c == '\r')
			continue;
		if (c != '\n')
		{
			line += c;
			continue;
		}
		if (!line.empty())
			Log(*transfer->settings, prefix + (headers ? RedactHeaderLine(line) : line));
		line.clear();
	}
	return 0;
}

} // namespace

std::string RedactHeaderLine(const std::string& line)
{
	const size_t colon = line.find(':');
	if (colon == std::string::npos)
		return line;
	const std::string name = Lower(line.substr(0, colon));
	if (name != "authorization" && name != "proxy-authorization" && name != "cookie" &&
		name != "set-cookie")
		return line;
	size_t start = colon + 1;
	while (start < line.size() && line[start] == ' ')
		++start;
	return line.substr(0, start) + Redact(line.substr(start));
}

bool CheckRequestUrl(const std::string& url, const HttpSettings& settings, bool& loopback,
	std::string& error)
{
	EnsureCurl();
	loopback = false;
	CURLU* parsed = curl_url();
	if (!parsed)
	{
		error = "out of memory";
		return false;
	}

	bool ok = false;
	char* scheme = nullptr;
	char* host = nullptr;
	char* user = nullptr;
	char* password = nullptr;
	if (curl_url_set(parsed, CURLUPART_URL, url.c_str(), 0) != CURLUE_OK ||
		curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
		curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK)
		error = "not a valid URL: " + url;
	else if (curl_url_get(parsed, CURLUPART_USER, &user, 0) == CURLUE_OK ||
			 curl_url_get(parsed, CURLUPART_PASSWORD, &password, 0) == CURLUE_OK)
		error = "a user name or password in the URL is not allowed";
	else
	{
		const std::string s = Lower(scheme);
		const std::string h = Lower(host);
		loopback = (h == "127.0.0.1") || (h == "[::1]") || (h == "localhost");
		if (s == "https")
			ok = true;
		else if (s == "http" && loopback && settings.allow_http_loopback)
			ok = true;
		else
			error = "not an https URL: " + url;
	}

	curl_free(scheme);
	curl_free(host);
	curl_free(user);
	curl_free(password);
	curl_url_cleanup(parsed);
	return ok;
}

namespace
{

// The host of a URL, in lower case, without a final dot; empty if none
std::string UrlHost(const std::string& url)
{
	std::string result;
	CURLU* parsed = curl_url();
	char* host = nullptr;
	if (parsed && curl_url_set(parsed, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK &&
		curl_url_get(parsed, CURLUPART_HOST, &host, 0) == CURLUE_OK)
		result = Lower(host);
	curl_free(host);
	curl_url_cleanup(parsed);
	if (!result.empty() && result.back() == '.')
		result.pop_back();
	return result;
}

// One POST; location gets the target of a 3xx answer (absolute), if any
bool PostOnce(const std::string& url, const std::string& body, const std::string& form_log,
	const HttpSettings& settings, HttpResult& result, std::string& location)
{
	result = HttpResult();
	location.clear();
	bool loopback = false;
	if (!CheckRequestUrl(url, settings, loopback, result.error))
	{
		result.outcome = HttpResult::Outcome::BadUrl;
		Log(settings, "* request refused: " + result.error);
		return false;
	}

	CURL* easy = curl_easy_init();
	if (!easy)
	{
		result.outcome = HttpResult::Outcome::Network;
		result.error = "libcurl could not be initialised";
		return false;
	}

	Transfer transfer;
	transfer.settings = &settings;
	transfer.form_log = form_log;
	char error_buffer[CURL_ERROR_SIZE] = "";
	curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
	headers = curl_slist_append(headers, "Expect:");

	curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
	curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
	const bool http = settings.allow_http_loopback && loopback;
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, http ? "http,https" : "https");
#else
	curl_easy_setopt(easy, CURLOPT_PROTOCOLS,
		http ? (long) (CURLPROTO_HTTP | CURLPROTO_HTTPS) : (long) CURLPROTO_HTTPS);
#endif
	// Redirects are checked and followed by PostForm(), never by libcurl
	curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(easy, CURLOPT_NETRC, (long) CURL_NETRC_IGNORED);
	// The cookie engine stays off (no CURLOPT_COOKIEFILE)
	if (loopback)
		curl_easy_setopt(easy, CURLOPT_NOPROXY, "*");
	curl_easy_setopt(easy, CURLOPT_USERAGENT, settings.user_agent.c_str());
	curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) body.size());
	curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, settings.connect_timeout);
	curl_easy_setopt(easy, CURLOPT_TIMEOUT, settings.timeout);
	curl_easy_setopt(easy, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t) settings.max_response_bytes);
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &Write);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, &transfer);
	curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, error_buffer);
	if (settings.cancel)
	{
		curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, &Progress);
		curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &transfer);
		curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
	}
	if (settings.log)
	{
		curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(easy, CURLOPT_DEBUGFUNCTION, &Debug);
		curl_easy_setopt(easy, CURLOPT_DEBUGDATA, &transfer);
	}

	Log(settings, "* POST " + url);
	const CURLcode code = curl_easy_perform(easy);
	if (code == CURLE_OK)
	{
		result.outcome = HttpResult::Outcome::Done;
		curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &result.status);
		char* type = nullptr;
		if (curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &type) == CURLE_OK && type)
			result.content_type = type;
		char* target = nullptr;
		if (curl_easy_getinfo(easy, CURLINFO_REDIRECT_URL, &target) == CURLE_OK && target)
			location = target;
		result.body.swap(transfer.body);
	}
	else if (transfer.too_large || code == CURLE_FILESIZE_EXCEEDED)
	{
		result.outcome = HttpResult::Outcome::TooLarge;
		result.error = "the answer is larger than the limit of " +
			std::to_string(settings.max_response_bytes) + " bytes";
	}
	else if (code == CURLE_OPERATION_TIMEDOUT)
	{
		result.outcome = HttpResult::Outcome::Timeout;
		result.error = "no answer within the time limit: ";
		result.error += error_buffer[0] ? error_buffer : curl_easy_strerror(code);
	}
	else if (code == CURLE_ABORTED_BY_CALLBACK)
	{
		result.outcome = HttpResult::Outcome::Cancelled;
		result.error = "cancelled";
	}
	else
	{
		result.outcome = HttpResult::Outcome::Network;
		result.error = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
	}
	if (result.outcome != HttpResult::Outcome::Done)
		Log(settings, "* request failed: " + result.error);

	curl_easy_cleanup(easy);
	curl_slist_free_all(headers);
	return result.outcome == HttpResult::Outcome::Done;
}

} // namespace

bool IsRedirectAllowed(const std::string& from, const std::string& to,
	const std::vector<std::string>& redirect_domains, const HttpSettings& settings,
	std::string& error)
{
	bool loopback = false;
	if (!CheckRequestUrl(to, settings, loopback, error))
		return false;
	const std::string host = UrlHost(to);
	if (!host.empty() && host == UrlHost(from))
		return true;
	for (std::string domain : redirect_domains)
	{
		domain = Lower(domain);
		if (!domain.empty() && domain.back() == '.')
			domain.pop_back();
		if (domain.empty())
			continue;
		if (host == domain ||
			(host.size() > domain.size() && host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
			 host[host.size() - domain.size() - 1] == '.'))
			return true;
	}
	error = "host " + host + " is neither the endpoint's host nor in the provider's redirect_domains";
	return false;
}

bool PostForm(const std::string& url, const Params& form,
	const std::vector<std::string>& redirect_domains, const HttpSettings& settings,
	HttpResult& result)
{
	const std::string body = BuildForm(form);
	const std::string form_log = RedactParams(body);
	std::string current = url;
	for (int redirects = 0; ; ++redirects)
	{
		std::string location;
		if (!PostOnce(current, body, form_log, settings, result, location))
			return false;
		const long status = result.status;
		const bool keeps_form = status == 307 || status == 308;
		const bool redirect = keeps_form || status == 301 || status == 302 || status == 303;
		if (!redirect || location.empty())
			return true;

		std::string why;
		if (!keeps_form)
			why = "only 307 and 308 keep the request and its form";
		else if (redirects >= cMaxRedirects)
			why = "more than " + std::to_string(cMaxRedirects) + " redirects";
		else if (IsRedirectAllowed(current, location, redirect_domains, settings, why))
		{
			Log(settings, "* following redirect " + std::to_string(status) + " to " + location);
			current = location;
			continue;
		}
		result.outcome = HttpResult::Outcome::RedirectRefused;
		result.error = "redirect " + std::to_string(status) + " to " + location + " not followed: " + why;
		Log(settings, "* " + result.error);
		return false;
	}
}

} // namespace oauth2
