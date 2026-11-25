/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Private: libsodium initialisation, shared by the library's sources

#ifndef OAUTH2_SODIUM_H
#define OAUTH2_SODIUM_H

namespace oauth2
{

// Calls sodium_init() once (thread-safe); stops the program if it fails
void EnsureSodium();

} // namespace oauth2

#endif
