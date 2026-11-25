/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Folders and names derived from the application identity
// (OAUTH2-PLAN.md, section 7, "Locations and priority")

#ifndef OAUTH2_PATHS_H
#define OAUTH2_PATHS_H

#include "oauth2/Identity.h"

#include <string>

namespace oauth2
{

// Name of the provider configuration files
extern const char* const cProviderFileName;	// "oauth2-providers.conf.json"

// The user's home folder: $HOME when it is an absolute path, else the
// password database; empty if neither is known
std::string HomeDirectory();

// $XDG_CONFIG_HOME/<id> when that variable holds an absolute path (XDG Base
// Directory specification), else ~/.config/<id>; empty without a home
std::string UserConfigDirectory(const Identity& identity);

// Prefix of the application's environment variables: the identifier in
// upper case, other characters than A-Z and 0-9 as '_' ("MULBERRY")
std::string EnvironmentPrefix(const Identity& identity);

// The path with the home folder shown as "~", for messages
std::string DisplayPath(const std::string& path);

} // namespace oauth2

#endif
