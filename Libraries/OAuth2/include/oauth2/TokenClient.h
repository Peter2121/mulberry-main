/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Token endpoint client (RFC 6749 sections 4.1.3, 5 and 6; OAUTH2-PLAN.md,
// task 1.4): authorization-code exchange and refresh, as form POSTs over
// libcurl (Http.h), with the client secret in the form when there is one.
// The functions block until the answer, the time limit or a cancellation;
// call them off the UI thread.

#ifndef OAUTH2_TOKENCLIENT_H
#define OAUTH2_TOKENCLIENT_H

#include "oauth2/Encoding.h"
#include "oauth2/Http.h"

#include <ctime>
#include <string>
#include <vector>

namespace oauth2
{

struct Provider;

// Where and as whom token requests are made
struct TokenEndpoint
{
	std::string url;
	std::string client_id;
	std::string client_secret;		// empty: none sent (public client)

	// Hosts besides the endpoint's own that a 307 or 308 redirect may lead
	// to (Provider::redirect_domains)
	std::vector<std::string> redirect_domains;
};

// The provider's token endpoint ({tenant} resolved), client ID, secret and
// redirect domains.
// Apply the account's own settings first (WithAccountSettings()).
TokenEndpoint TokenEndpointOf(const Provider& provider);

// A successful answer (RFC 6749 section 5.1)
struct TokenResponse
{
	std::string access_token;
	std::string token_type;			// "Bearer" (only bearer tokens are accepted)
	long expires_in = -1;			// seconds; -1: not given
	std::string refresh_token;		// empty: not given (keep the current one)
	bool scope_given = false;		// false: the scopes requested were granted
	std::vector<std::string> scopes;
	std::string id_token;			// OpenID Connect; empty: not given
	long refresh_token_expires_in = -1;	// Google, for apps in testing; -1: not given
	std::time_t received_at = 0;	// when the request was sent (expiry base)
};

// Why a request failed
struct TokenError
{
	enum class Kind
	{
		None,
		Config,			// refused before sending: missing client ID, bad URL...
		Network,		// connection or TLS failure
		Timeout,
		Cancelled,
		Http,			// an HTTP status without an OAuth2 error (500, a refused redirect...)
		Protocol,		// not valid JSON, no access_token, too large...
		OAuth			// the server's OAuth2 error (RFC 6749 section 5.2)
	};

	Kind kind = Kind::None;
	long http_status = 0;			// 0: no HTTP answer
	std::string error;				// "invalid_grant"...
	std::string description;		// error_description (first line, cleaned)
	std::string uri;				// error_uri
	std::string subtype;			// Google error_subtype, Microsoft suberror
	long aadsts = 0;				// Microsoft AADSTS code; 0: none
	std::string correlation_id;		// Microsoft, for support requests
	std::string message;			// one readable line, never a secret

	// invalid_grant: the grant (code or refresh token) is expired, revoked
	// or already used; the user has to sign in again
	bool RequiresSignIn() const;

	// invalid_client or unauthorized_client: the registration (client ID,
	// secret, tenant) is wrong; signing in again does not help
	bool IsClientProblem() const;
};

const char* KindName(TokenError::Kind kind);

// Any token request: grant holds grant_type and its parameters; the
// client_id and client_secret of endpoint are added
bool RequestToken(const TokenEndpoint& endpoint, const Params& grant,
	const HttpSettings& settings, TokenResponse& response, TokenError& error);

// grant_type=authorization_code.  code_verifier: the PKCE verifier, empty
// when the provider does not use PKCE.  redirect_uri: exactly the one of the
// authorization request.
bool ExchangeCode(const TokenEndpoint& endpoint, const std::string& code,
	const std::string& code_verifier, const std::string& redirect_uri,
	const HttpSettings& settings, TokenResponse& response, TokenError& error);

// Checks the registration before the user is sent to the browser, where a
// wrong client ID or secret would only show up after the sign-in, or as a
// provider's error page that never comes back to the listener.  Apply the
// account's own settings first (WithAccountSettings()).  By the provider's
// client_check:
//
// - "token": an authorization-code exchange with an invalid code.  The
//   provider answers invalid_client or unauthorized_client for a wrong
//   registration, and invalid_grant (or another error) for a good one.
// - "device_authorization": a device code request with the client ID and
//   scopes (not the secret), for a provider that checks the code before the
//   client (Microsoft: invalid_grant AADSTS9002313 for any client).  A
//   device code means a good client; it is not used and expires.  Microsoft
//   AADSTS50059 and AADSTS700016 mean an unknown client, as do
//   invalid_client and unauthorized_client.  The secret is not checked.
//
// False, with error, when the provider refuses the client or cannot be
// reached (network, timeout, configuration, cancelled): the sign-in would
// fail anyway.  error.kind is then OAuth only for a refused client.  True
// for any other answer, with error holding it (for the log).
bool ProbeClient(const Provider& provider, const std::vector<std::string>& scopes,
	const std::string& redirect_uri, const HttpSettings& settings, TokenError& error);

// grant_type=refresh_token.  scopes: sent as "scope" when not empty
// (Microsoft expects it).
bool RefreshToken(const TokenEndpoint& endpoint, const std::string& refresh_token,
	const std::vector<std::string>& scopes, const HttpSettings& settings,
	TokenResponse& response, TokenError& error);

// Interprets a token endpoint answer (used by the functions above; public
// for tests and for the device code polling)
bool ParseTokenResponse(long http_status, const std::string& content_type,
	const std::string& body, std::time_t received_at, TokenResponse& response,
	TokenError& error);

// A JSON answer for the log: the values of SecretFieldNames() replaced by
// Redact(); anything else than a JSON object becomes "(N bytes, not JSON)"
std::string RedactJsonBody(const std::string& body);

// The tokens of one grant, as kept between requests
struct Tokens
{
	std::string access_token;
	std::time_t expires_at = 0;		// 0: unknown
	std::string refresh_token;
	std::vector<std::string> scopes;
	std::string id_token;

	// True when the access token is missing, or expires within margin
	// seconds of now (an unknown expiry counts as expired)
	bool NeedsRefresh(std::time_t now, long margin = 60) const;
};

// Applies an answer: the new access token always; a new refresh token
// replaces the old one (rotation), none keeps it; the scopes and ID token
// when given.  Returns true when the refresh token changed (to be saved).
bool UpdateTokens(Tokens& tokens, const TokenResponse& response);

} // namespace oauth2

#endif
