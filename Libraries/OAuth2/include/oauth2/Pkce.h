/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// PKCE, Proof Key for Code Exchange (RFC 7636), with the S256 method only

#ifndef OAUTH2_PKCE_H
#define OAUTH2_PKCE_H

#include <string>

namespace oauth2
{

struct Pkce
{
	// Sent with the token request ("code_verifier"); a secret until then
	std::string verifier;
	// Sent with the authorization request ("code_challenge")
	std::string challenge;
	// "code_challenge_method"
	static constexpr const char* cMethod = "S256";
};

// A new verifier (32 random bytes, base64url: 43 characters, the length
// RFC 7636 section 4.1 recommends) and its challenge
Pkce MakePkce();

// BASE64URL(SHA256(ASCII(verifier))), RFC 7636 section 4.2
std::string PkceChallengeS256(const std::string& verifier);

// True if the verifier has the form RFC 7636 section 4.1 requires: 43 to
// 128 characters from A-Z a-z 0-9 '-' '.' '_' '~'
bool IsValidPkceVerifier(const std::string& verifier);

} // namespace oauth2

#endif
