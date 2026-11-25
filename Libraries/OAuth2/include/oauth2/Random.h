/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Random values from libsodium (decision D2), and the OAuth2 "state"
// parameter

#ifndef OAUTH2_RANDOM_H
#define OAUTH2_RANDOM_H

#include <cstddef>
#include <string>

namespace oauth2
{

// count bytes from the operating system's secure random generator
// (libsodium randombytes_buf).  If libsodium cannot be initialised (no
// random source at all), the program is stopped: there is no safe way on.
std::string RandomBytes(size_t count);

// A new "state" value for an authorization request (RFC 6749 section
// 10.12): 128 random bits, base64url, 22 characters
std::string MakeState();

// Compares two strings in a time that does not depend on where they differ
// (for the returned "state"); false if the lengths differ
bool ConstantTimeEquals(const std::string& a, const std::string& b);

} // namespace oauth2

#endif
