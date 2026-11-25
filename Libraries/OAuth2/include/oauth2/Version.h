/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Versions of the OAuth2 core and of the libcurl it runs with

#ifndef OAUTH2_VERSION_H
#define OAUTH2_VERSION_H

#include <string>

namespace oauth2
{

// Version of this library ("0.1.0")
const char* LibraryVersion();

// Version of the libcurl loaded at run time ("8.22.0"), and its TLS
// backend ("OpenSSL/3.5.4"); empty if libcurl does not report one
std::string CurlVersion();
std::string CurlTlsBackend();

} // namespace oauth2

#endif
