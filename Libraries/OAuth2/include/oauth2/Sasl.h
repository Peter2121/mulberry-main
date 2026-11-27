/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// SASL mechanisms for OAuth2 access tokens (OAUTH2-PLAN.md, task 1.8), for
// IMAP, SMTP, POP3 and ManageSieve (Mulberry's protocol code, stage 4):
//
// - XOAUTH2 (Google's protocol, also Microsoft's and Yahoo's): the initial
//   response "user=" USER ^A "auth=Bearer " TOKEN ^A ^A;
// - OAUTHBEARER (RFC 7628): a GS2 header "n,a=" USER "," then key=value
//   pairs, each ended by ^A: host, port, auth; then a final ^A.
//
// The responses are returned before base64: the protocol code encodes them
// (Base64Encode(), Encoding.h) and sends them as the initial response or
// after the server's empty challenge.  They hold the access token: never
// log them.
//
// When the token is refused, the server first sends a challenge: base64 of
// a JSON object (status, schemes, scope; RFC 7628 adds
// openid-configuration).  The client answers it with SaslErrorReply() and
// gets the final failure (IMAP NO, SMTP 535).  ParseSaslErrorChallenge()
// reads that object.

#ifndef OAUTH2_SASL_H
#define OAUTH2_SASL_H

#include <string>

namespace oauth2
{

extern const char* const cSaslXOAuth2;		// "XOAUTH2"
extern const char* const cSaslOAuthBearer;	// "OAUTHBEARER"

// The XOAUTH2 initial client response.  Empty when the user or the token
// is empty, or holds a control character (^A would end a field early).
std::string XOAuth2Response(const std::string& user, const std::string& access_token);

// The OAUTHBEARER initial client response (RFC 7628 section 3.1).  The user
// becomes an RFC 5801 saslname: "," is written "=2C" and "=" "=3D".  host
// empty or port 0: the pair is left out (optional with bearer tokens).
// Empty when the user or the token is empty, or when the user, host or
// token holds a control character.
std::string OAuthBearerResponse(const std::string& user, const std::string& host, int port,
	const std::string& access_token);

// RFC 5801 saslname escaping, as in the GS2 header
std::string Gs2SaslName(const std::string& user);

// The answer to the server's error challenge, before base64: OAUTHBEARER a
// single ^A (RFC 7628 section 3.2.3, "AQ==" once encoded); XOAUTH2 an
// empty response (Google).  Empty for an unknown mechanism too.
std::string SaslErrorReply(const std::string& mechanism);

// What the server said in its error challenge.  Texts are cleaned (one
// line, no control characters, at most 300 bytes).
struct SaslError
{
	std::string status;					// "invalid_token" (RFC 7628), "401", "400" (Google)
	std::string schemes;				// "bearer"...
	std::string scope;					// the scope the server needs
	std::string openid_configuration;	// RFC 7628: the provider's discovery URL

	// One line: "status 401 (the access token is invalid or expired), scope
	// https://mail.google.com/"
	std::string Describe() const;
};

// Decodes a challenge received during an OAuth2 SASL exchange: base64 (as
// on the wire) of a JSON object.  False, with message and error unchanged,
// when it is not base64 or not a JSON object; a non-string member is
// ignored.  The message never quotes the challenge.
bool ParseSaslErrorChallenge(const std::string& challenge, SaslError& error, std::string& message);

} // namespace oauth2

#endif
