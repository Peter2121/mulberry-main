/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Random.h"

#include "oauth2/Encoding.h"
#include "Sodium.h"

#include <sodium.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace oauth2
{

void EnsureSodium()
{
	static std::once_flag sOnce;
	std::call_once(sOnce, []()
	{
		// 0: initialised now, 1: already initialised (by the host), -1:
		// failure (no random source)
		if (sodium_init() < 0)
		{
			std::fprintf(stderr, "oauth2: libsodium cannot be initialised (no secure random source)\n");
			std::abort();
		}
	});
}

std::string RandomBytes(size_t count)
{
	EnsureSodium();
	std::string result(count, '\0');
	if (count > 0)
		randombytes_buf(&result[0], count);
	return result;
}

std::string MakeState()
{
	return Base64UrlEncode(RandomBytes(16));
}

bool ConstantTimeEquals(const std::string& a, const std::string& b)
{
	if (a.size() != b.size())
		return false;
	if (a.empty())
		return true;
	EnsureSodium();
	return sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}

} // namespace oauth2
