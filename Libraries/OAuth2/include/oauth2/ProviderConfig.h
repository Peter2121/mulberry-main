/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// The provider configuration: the system file, then the user's file over it
// (OAUTH2-PLAN.md, section 7).  Both are parsed strictly; any error stops the
// loading with a message naming the file, line and column, and the host
// application then reports it and exits.

#ifndef OAUTH2_PROVIDERCONFIG_H
#define OAUTH2_PROVIDERCONFIG_H

#include "oauth2/Identity.h"
#include "oauth2/Provider.h"

#include <cstddef>
#include <string>
#include <vector>

namespace oauth2
{

struct ConfigError
{
	std::string file;	// as given (DisplayPath() shortens it for messages)
	size_t line = 0;	// 1-based; 0 when the error has no position
	size_t column = 0;	// 1-based, in characters
	std::string message;

	// "~/.config/mulberry/oauth2-providers.conf.json:14:7: unknown key ..."
	std::string ToString() const;
};

struct ConfigFiles
{
	std::string system_file;	// empty: none
	std::string user_file;		// empty: none

	// The system file was named explicitly (environment variable, or an
	// option of the test tool), to run from the build tree: it is then not
	// expected to belong to root
	bool system_file_explicit = false;
};

// The usual locations: <sysconfdir>/<id>/oauth2-providers.conf.json, or the
// file named by $<PREFIX>_OAUTH2_SYSTEM_CONFIG (MULBERRY_OAUTH2_SYSTEM_CONFIG)
// when set; and UserConfigDirectory()/oauth2-providers.conf.json
ConfigFiles DefaultConfigFiles(const Identity& identity, const std::string& sysconfdir);

// Name of the environment variable that replaces the system file
std::string SystemConfigVariable(const Identity& identity);

class ProviderConfig
{
public:
	// Reads both files.  A missing file is not an error (see
	// SystemFileFound()); an unreadable or invalid one is.
	bool Load(const ConfigFiles& files, ConfigError& error);

	// The same from texts (tests); an empty name means "no such file"
	bool LoadTexts(const std::string& system_name, const std::string& system_text,
		const std::string& user_name, const std::string& user_text, ConfigError& error);

	// False when there is no system file: OAuth2 is then unavailable, and
	// lookups find nothing (the user file is still checked)
	bool SystemFileFound() const { return mSystemFound; }
	const std::string& SystemFile() const { return mSystemFile; }

	// The enabled providers, in the system file's order, then the user's
	const std::vector<Provider>& Providers() const { return mProviders; }

	const Provider* Find(const std::string& id) const;
	// By server name (case-insensitive, trailing dot ignored)
	const Provider* FindByHost(const std::string& host) const;
	// By the domain of an e-mail address
	const Provider* FindByEmail(const std::string& address) const;

private:
	std::vector<Provider> mProviders;
	bool mSystemFound = false;
	std::string mSystemFile;
};

} // namespace oauth2

#endif
