/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Pkce.h"

#include "oauth2/Encoding.h"
#include "oauth2/Random.h"
#include "Sodium.h"

#include <sodium.h>

namespace oauth2
{

Pkce MakePkce()
{
	Pkce pkce;
	pkce.verifier = Base64UrlEncode(RandomBytes(32));
	pkce.challenge = PkceChallengeS256(pkce.verifier);
	return pkce;
}

std::string PkceChallengeS256(const std::string& verifier)
{
	EnsureSodium();
	unsigned char digest[crypto_hash_sha256_BYTES];
	crypto_hash_sha256(digest, reinterpret_cast<const unsigned char*>(verifier.data()),
		verifier.size());
	return Base64UrlEncode(std::string(reinterpret_cast<const char*>(digest), sizeof(digest)));
}

bool IsValidPkceVerifier(const std::string& verifier)
{
	if (verifier.size() < 43 || verifier.size() > 128)
		return false;
	for (char c : verifier)
	{
		const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
		if (!ok)
			return false;
	}
	return true;
}

} // namespace oauth2
