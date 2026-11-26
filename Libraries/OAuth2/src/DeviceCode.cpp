/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/DeviceCode.h"

#include "oauth2/Provider.h"
#include "oauth2/Redact.h"
#include "HttpClient.h"
#include "TokenInternal.h"

#include <errno.h>
#include <time.h>

#include <algorithm>
#include <chrono>

namespace oauth2
{

using namespace internal;

namespace
{

const char* const cDeviceGrant = "urn:ietf:params:oauth:grant-type:device_code";

// RFC 8628 section 3.2: 5 seconds when the server says nothing
const long cDefaultInterval = 5;
// Section 3.5: slow_down adds 5 seconds
const long cSlowDownStep = 5;
// Failed polls (network, time limit, HTTP 5xx) tried again in a row
const int cMaxTransientFailures = 3;
// Longest wait between two polls after failures
const long cMaxBackoff = 60;
// Longest URL and user code accepted from the server
const size_t cMaxUrl = 2048;
const size_t cMaxUserCode = 64;
// Steps of the wait between polls, to notice a cancellation
const long cSleepStepMs = 100;

typedef std::chrono::steady_clock Clock;

// http:// or https://, no space or control character, not too long: shown
// to the user as it is
bool IsPlainUrl(const std::string& url)
{
	if (url.size() > cMaxUrl || (url.compare(0, 8, "https://") != 0 && url.compare(0, 7, "http://") != 0))
		return false;
	const size_t host = url.find("://") + 3;
	if (host >= url.size() || url[host] == '/')
		return false;
	return std::none_of(url.begin(), url.end(),
		[](char c) { return (unsigned char) c <= 0x20 || (unsigned char) c >= 0x7f; });
}

// Printable ASCII without spaces (RFC 8628 section 6.1: the user types it)
bool IsUserCode(const std::string& code)
{
	return !code.empty() && code.size() <= cMaxUserCode &&
		std::all_of(code.begin(), code.end(),
			[](char c) { return (unsigned char) c > 0x20 && (unsigned char) c < 0x7f; });
}

// Waits until the time point, or less if cancel is set; false when
// cancelled
bool SleepUntil(Clock::time_point until, const std::atomic<bool>* cancel)
{
	for (;;)
	{
		if (cancel && *cancel)
			return false;
		const Clock::time_point now = Clock::now();
		if (now >= until)
			return true;
		const long left = (long) std::chrono::duration_cast<std::chrono::milliseconds>(until - now).count();
		const long step = std::min(left, cSleepStepMs);
		struct timespec delay;
		delay.tv_sec = step / 1000;
		delay.tv_nsec = (step % 1000) * 1000000L;
		while (nanosleep(&delay, &delay) == -1 && errno == EINTR)
		{
			if (cancel && *cancel)
				return false;
		}
	}
}

// A failure worth another try: no answer, or a server error
bool IsTransient(const TokenError& error)
{
	return error.kind == TokenError::Kind::Network || error.kind == TokenError::Kind::Timeout ||
		(error.kind == TokenError::Kind::Http && error.http_status >= 500);
}

} // namespace

bool ParseDeviceAuthorization(long http_status, const std::string& content_type,
	const std::string& body, std::time_t received_at, DeviceAuthorization& authorization,
	TokenError& error)
{
	authorization = DeviceAuthorization();
	error = TokenError();
	error.http_status = http_status;

	Json json;
	try
	{
		json = Json::parse(body, nullptr, false);
	}
	catch (const std::exception&)
	{
		json = Json(Json::value_t::discarded);
	}
	const bool object = !json.is_discarded() && json.is_object();

	if (object && json.contains("error"))
	{
		SetOAuthError(json, http_status, error);
		return false;
	}
	if (http_status != 200)
	{
		SetError(error, TokenError::Kind::Http, "HTTP " + std::to_string(http_status) +
			" from the device authorization endpoint (" + DescribeBody(content_type, body) + ")");
		return false;
	}
	// Never the parser's message or the answer: it holds the device code
	if (!object)
	{
		SetError(error, TokenError::Kind::Protocol, "the device authorization endpoint's answer is not a "
			"JSON object (" + DescribeBody(content_type, body) + ")");
		return false;
	}

	DeviceAuthorization parsed;
	parsed.received_at = received_at;
	const auto missing = [&error](const char* what)
	{
		SetError(error, TokenError::Kind::Protocol, std::string("the device authorization endpoint's answer "
			"has no valid ") + what);
		return false;
	};

	parsed.device_code = StringField(json, "device_code");
	if (parsed.device_code.empty())
		return missing("device_code");
	parsed.user_code = StringField(json, "user_code");
	if (!IsUserCode(parsed.user_code))
		return missing("user_code (printable characters without spaces)");
	parsed.verification_uri = StringField(json, "verification_uri");
	if (parsed.verification_uri.empty())
		parsed.verification_uri = StringField(json, "verification_url");	// Google's older name
	if (!IsPlainUrl(parsed.verification_uri))
		return missing("verification_uri (an http or https URL)");
	parsed.verification_uri_complete = StringField(json, "verification_uri_complete");
	if (!IsPlainUrl(parsed.verification_uri_complete))
		parsed.verification_uri_complete.clear();
	parsed.message = CleanText(StringField(json, "message"));

	const auto expires = json.find("expires_in");
	if (expires == json.end() || !ParseSeconds(*expires, parsed.expires_in) || parsed.expires_in <= 0)
		return missing("expires_in");
	const auto interval = json.find("interval");
	if (interval == json.end() || !ParseSeconds(*interval, parsed.interval) || parsed.interval < 1)
		parsed.interval = cDefaultInterval;

	authorization = std::move(parsed);
	return true;
}

bool RequestDeviceAuthorization(const Provider& provider, const std::vector<std::string>& scopes,
	const HttpSettings& settings, DeviceAuthorization& authorization, TokenError& error)
{
	authorization = DeviceAuthorization();
	error = TokenError();
	const auto log = [&settings](const std::string& line) { if (settings.log) settings.log(line); };

	if (!provider.SupportsDeviceCode())
		SetError(error, TokenError::Kind::Config, "the provider offers no device code sign-in");
	else if (provider.client_id.empty())
		SetError(error, TokenError::Kind::Config, "no client ID for this provider");
	if (error.kind != TokenError::Kind::None)
	{
		log("* device code request refused: " + error.message);
		return false;
	}

	Params form = { {"client_id", provider.client_id} };
	if (!scopes.empty())
		form.emplace_back("scope", JoinWords(scopes));
	// RFC 8628 section 3.1: a confidential client authenticates
	if (!provider.client_secret.empty())
		form.emplace_back("client_secret", provider.client_secret);

	const std::time_t sent_at = std::time(nullptr);
	HttpResult result;
	if (!PostForm(provider.ResolveEndpoint(provider.device_authorization_endpoint), form,
		provider.redirect_domains, settings, result))
	{
		SetTransportError(error, result, "device authorization endpoint", "device code request");
		return false;
	}
	if (!result.body.empty())
		log("< " + RedactJsonBody(result.body));
	if (!ParseDeviceAuthorization(result.status, result.content_type, result.body, sent_at, authorization,
		error))
	{
		log("* device code request failed: " + error.message);
		return false;
	}
	log("* device code received: user code " + authorization.user_code + ", expires in " +
		std::to_string(authorization.expires_in) + " s, poll every " + std::to_string(authorization.interval) +
		" s");
	return true;
}

bool IsUnknownClient(const TokenError& error)
{
	return error.kind == TokenError::Kind::OAuth &&
		(error.IsClientProblem() || error.aadsts == 50059 || error.aadsts == 700016);
}

const char* DevicePollName(DevicePoll poll)
{
	switch (poll)
	{
	case DevicePoll::Granted:	return "granted";
	case DevicePoll::Pending:	return "pending";
	case DevicePoll::SlowDown:	return "slow down";
	case DevicePoll::Denied:	return "denied";
	case DevicePoll::Expired:	return "expired";
	case DevicePoll::Failed:	return "failed";
	}
	return "unknown";
}

DevicePoll ClassifyDevicePoll(const TokenError& error)
{
	if (error.kind == TokenError::Kind::None)
		return DevicePoll::Granted;
	if (error.kind != TokenError::Kind::OAuth)
		return DevicePoll::Failed;
	if (error.error == "authorization_pending")
		return DevicePoll::Pending;
	if (error.error == "slow_down")
		return DevicePoll::SlowDown;
	if (error.error == "access_denied" || error.error == "authorization_declined")
		return DevicePoll::Denied;
	if (error.error == "expired_token")
		return DevicePoll::Expired;
	return DevicePoll::Failed;
}

DevicePoll PollDeviceToken(const TokenEndpoint& endpoint, const std::string& device_code,
	const HttpSettings& settings, TokenResponse& response, TokenError& error)
{
	response = TokenResponse();
	error = TokenError();
	if (device_code.empty())
	{
		SetError(error, TokenError::Kind::Config, "no device code");
		return DevicePoll::Failed;
	}
	const Params grant = {
		{"grant_type", cDeviceGrant},
		{"device_code", device_code}
	};
	if (RequestToken(endpoint, grant, settings, response, error))
		return DevicePoll::Granted;
	return ClassifyDevicePoll(error);
}

bool WaitForDeviceToken(const TokenEndpoint& endpoint, const DeviceAuthorization& authorization,
	long max_wait, const HttpSettings& settings, TokenResponse& response, TokenError& error)
{
	response = TokenResponse();
	error = TokenError();
	const auto log = [&settings](const std::string& line) { if (settings.log) settings.log(line); };

	// The code's remaining lifetime, measured on a clock that the user
	// cannot move
	const long elapsed = std::max(0L, (long) (std::time(nullptr) - authorization.received_at));
	long remaining = std::max(0L, authorization.expires_in - elapsed);
	const bool limited = max_wait > 0 && max_wait < remaining;
	if (limited)
		remaining = max_wait;
	const Clock::time_point deadline = Clock::now() + std::chrono::seconds(remaining);

	long interval = std::max(1L, authorization.interval);
	long wait = interval;
	int failures = 0;
	for (;;)
	{
		if (!SleepUntil(std::min(Clock::now() + std::chrono::seconds(wait), deadline), settings.cancel))
		{
			SetError(error, TokenError::Kind::Cancelled, "cancelled");
			return false;
		}

		const DevicePoll poll = PollDeviceToken(endpoint, authorization.device_code, settings, response, error);
		wait = interval;
		switch (poll)
		{
		case DevicePoll::Granted:
			log("* device code: signed in");
			return true;
		case DevicePoll::Pending:
			failures = 0;
			break;
		case DevicePoll::SlowDown:
			failures = 0;
			interval += cSlowDownStep;
			wait = interval;
			log("* device code: slow_down, polling every " + std::to_string(interval) + " s");
			break;
		case DevicePoll::Failed:
			if (error.kind == TokenError::Kind::Cancelled)
				return false;
			if (IsTransient(error) && failures < cMaxTransientFailures)
			{
				++failures;
				wait = std::min(cMaxBackoff, interval << failures);
				log("* device code: poll failed (" + error.message + "), trying again in " +
					std::to_string(wait) + " s");
				break;
			}
			return false;
		default:
			// Denied, Expired: as the provider said
			return false;
		}

		if (Clock::now() >= deadline)
		{
			response = TokenResponse();
			error = TokenError();
			SetError(error, TokenError::Kind::Timeout, limited ?
				"no sign-in within " + std::to_string(max_wait) + " s" :
				"the code expired after " + std::to_string(authorization.expires_in) + " s without a sign-in");
			return false;
		}
	}
}

} // namespace oauth2
