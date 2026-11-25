/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Paths.h"

#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <vector>

namespace oauth2
{

const char* const cProviderFileName = "oauth2-providers.conf.json";

std::string HomeDirectory()
{
	const char* home = std::getenv("HOME");
	if (home && home[0] == '/')
		return home;

	// Thread-safe lookup in the password database
	long size = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (size <= 0)
		size = 16384;
	std::vector<char> buffer(static_cast<size_t>(size));
	struct passwd entry;
	struct passwd* result = nullptr;
	if (getpwuid_r(geteuid(), &entry, buffer.data(), buffer.size(), &result) == 0 && result &&
		result->pw_dir && result->pw_dir[0] == '/')
		return result->pw_dir;
	return std::string();
}

std::string UserConfigDirectory(const Identity& identity)
{
	const char* xdg = std::getenv("XDG_CONFIG_HOME");
	if (xdg && xdg[0] == '/')
		return std::string(xdg) + "/" + identity.id;
	const std::string home = HomeDirectory();
	if (home.empty())
		return std::string();
	return home + "/.config/" + identity.id;
}

std::string EnvironmentPrefix(const Identity& identity)
{
	std::string result;
	for (char c : identity.id)
	{
		if (c >= 'a' && c <= 'z')
			result += static_cast<char>(c - 'a' + 'A');
		else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			result += c;
		else
			result += '_';
	}
	return result;
}

std::string DisplayPath(const std::string& path)
{
	std::string home = HomeDirectory();
	while (home.size() > 1 && home.back() == '/')
		home.pop_back();
	if (home.size() > 1 && path.compare(0, home.size(), home) == 0 &&
		(path.size() == home.size() || path[home.size()] == '/'))
		return "~" + path.substr(home.size());
	return path;
}

} // namespace oauth2
