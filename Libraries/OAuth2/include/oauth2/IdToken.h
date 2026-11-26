/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// The claims of an OpenID Connect ID token (a JWT), to show and check who
// signed in (OAUTH2-PLAN.md, tasks 1.6 and 3.5).
//
// The signature is not verified.  OpenID Connect Core 1.0, section 3.1.3.7,
// allows this when the token comes straight from the token endpoint over
// TLS, which is the only way this library receives one.  An ID token from
// anywhere else must not be trusted with this parser.

#ifndef OAUTH2_IDTOKEN_H
#define OAUTH2_IDTOKEN_H

#include <ctime>
#include <string>
#include <vector>

namespace oauth2
{

struct IdTokenClaims
{
	std::string issuer;				// iss
	std::string subject;			// sub
	std::vector<std::string> audience;	// aud (a string or a list)
	std::string email;				// email; empty: not given
	bool email_verified = false;	// email_verified (true or "true")
	bool email_verified_given = false;	// the claim is present (Microsoft: never)
	std::string preferred_username;	// Microsoft: usually the sign-in address
	std::string name;
	std::time_t expires_at = 0;		// exp; 0: not given
	std::time_t issued_at = 0;		// iat; 0: not given

	// The e-mail address of the user: email, else preferred_username when
	// it looks like an address; empty if neither
	std::string Address() const;

	bool HasAudience(const std::string& client_id) const;
};

// Decodes the payload of "header.payload.signature".  False, with error and
// nothing changed, when the token does not have three parts, the payload is
// not base64url, not a JSON object, or a known claim has the wrong type.
bool ParseIdToken(const std::string& token, IdTokenClaims& claims, std::string& error);

} // namespace oauth2

#endif
