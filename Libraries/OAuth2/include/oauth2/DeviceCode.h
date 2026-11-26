/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Device authorization grant (RFC 8628; OAUTH2-PLAN.md, task 1.7): the
// user signs in on another device (a phone) by typing a short code at the
// provider's address, while the application polls the token endpoint.  For
// providers with a device_authorization_endpoint (Microsoft; not Google for
// mail scopes).  The functions block; call them off the UI thread.

#ifndef OAUTH2_DEVICECODE_H
#define OAUTH2_DEVICECODE_H

#include "oauth2/Http.h"
#include "oauth2/TokenClient.h"

#include <ctime>
#include <string>
#include <vector>

namespace oauth2
{

struct Provider;

// The answer to the device authorization request (RFC 8628 section 3.2).
// Every text is checked or cleaned, so it can be shown as it is.
struct DeviceAuthorization
{
	std::string device_code;				// secret: never shown, redacted in logs
	std::string user_code;					// what the user types: printable ASCII, no spaces
	std::string verification_uri;			// where: http or https, no spaces
	std::string verification_uri_complete;	// the same with the code in it; empty: not given
	std::string message;					// the provider's instruction (Microsoft), one line; may be empty
	long expires_in = 0;					// seconds, more than 0
	long interval = 5;						// seconds between polls, at least 1 (RFC default 5)
	std::time_t received_at = 0;			// when the request was sent (expiry base)
};

// Interprets the answer of the device authorization endpoint.  False, with
// error, for an OAuth2 error (as TokenError of the token endpoint), an
// HTTP error, or an answer without device_code, user_code,
// verification_uri (or Google's verification_url) and expires_in.  A
// verification_uri_complete that is not a plain http(s) URL is dropped.
// Messages never quote the answer, which holds the device code.
bool ParseDeviceAuthorization(long http_status, const std::string& content_type,
	const std::string& body, std::time_t received_at, DeviceAuthorization& authorization,
	TokenError& error);

// POSTs client_id, scope (and the client secret when there is one) to the
// provider's device_authorization_endpoint ({tenant} resolved).  Apply the
// account's own settings first (WithAccountSettings()).
bool RequestDeviceAuthorization(const Provider& provider, const std::vector<std::string>& scopes,
	const HttpSettings& settings, DeviceAuthorization& authorization, TokenError& error);

// Whether an error of a device authorization request means that the
// provider does not know the client: invalid_client, unauthorized_client,
// or Microsoft's AADSTS700016 ("application not found") and AADSTS50059
// ("no tenant-identifying information": on /common and /organizations,
// only the application would tell the tenant)
bool IsUnknownClient(const TokenError& error);

// What one poll of the token endpoint brought (RFC 8628 section 3.5)
enum class DevicePoll
{
	Granted,	// tokens received
	Pending,	// authorization_pending: the user has not finished yet
	SlowDown,	// slow_down: poll 5 seconds less often
	Denied,		// access_denied (Microsoft: authorization_declined)
	Expired,	// expired_token, or the code's lifetime is over
	Failed		// any other error (bad_verification_code, invalid_client...)
};

const char* DevicePollName(DevicePoll poll);

// The meaning of a failed poll's error (public for tests)
DevicePoll ClassifyDevicePoll(const TokenError& error);

// One token request with grant_type
// urn:ietf:params:oauth:grant-type:device_code
DevicePoll PollDeviceToken(const TokenEndpoint& endpoint, const std::string& device_code,
	const HttpSettings& settings, TokenResponse& response, TokenError& error);

// Polls every interval seconds until the user signs in (true, with
// response).  Otherwise false, with error:
// - the user refused, the provider declared the code expired, or another
//   OAuth2 error: as the token endpoint said (ClassifyDevicePoll() tells
//   which);
// - the code's lifetime, or max_wait seconds (0: no limit), passed:
//   kind Timeout;
// - settings.cancel was set: kind Cancelled.
// slow_down adds 5 seconds to the interval.  A network failure, a time
// limit or an HTTP 5xx answer is tried again with a doubled interval (at
// most 60 seconds), up to 3 times in a row.  The log gets one line per poll.
bool WaitForDeviceToken(const TokenEndpoint& endpoint, const DeviceAuthorization& authorization,
	long max_wait, const HttpSettings& settings, TokenResponse& response, TokenError& error);

} // namespace oauth2

#endif
