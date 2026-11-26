/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// The authorization request of the browser sign-in (RFC 6749 section 4.1.1,
// RFC 7636 section 4.3; OAUTH2-PLAN.md, task 1.6), and what the answer
// granted compared with what was asked.

#ifndef OAUTH2_AUTHORIZATION_H
#define OAUTH2_AUTHORIZATION_H

#include "oauth2/Encoding.h"

#include <string>
#include <vector>

namespace oauth2
{

struct Provider;

struct AuthorizationRequest
{
	std::vector<std::string> scopes;	// Provider::ScopesFor()
	std::string redirect_uri;			// LoopbackListener::RedirectUri()
	std::string state;					// MakeState()
	std::string code_challenge;			// MakePkce().challenge; empty: no PKCE
	std::string login_hint;				// the account's e-mail address; empty: none

	// Replaces a parameter of the provider's authorization_params with the
	// same name; an empty value removes it ("prompt": "" sends no prompt).
	// The parameters set above cannot be replaced.
	Params overrides;
};

// The URL to open in the browser: the authorization endpoint ({tenant}
// resolved) with response_type=code, client_id, redirect_uri, scope, state,
// code_challenge and code_challenge_method=S256 (when given and the
// provider uses PKCE), login_hint, then the provider's authorization_params
// in name order, as changed by overrides.
std::string BuildAuthorizationUrl(const Provider& provider, const AuthorizationRequest& request);

// The scopes asked for that the answer did not grant, when the answer lists
// its scopes (scope_given); the provider's "always" scopes are not checked,
// as providers report them under other names ("email" comes back as
// ".../auth/userinfo.email") or not at all ("offline_access").
std::vector<std::string> MissingScopes(const Provider& provider,
	const std::vector<std::string>& requested, bool scope_given,
	const std::vector<std::string>& granted);

// The scopes for a refresh request: those asked for at sign-in, minus the
// missing ones (asking again for a scope the user refused makes Google
// fail with invalid_scope)
std::vector<std::string> RefreshScopes(const Provider& provider,
	const std::vector<std::string>& requested, bool scope_given,
	const std::vector<std::string>& granted);

} // namespace oauth2

#endif
