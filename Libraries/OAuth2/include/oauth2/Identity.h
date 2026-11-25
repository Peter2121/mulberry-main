/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// The host application's identity (OAUTH2-PLAN.md, section 3, "No
// hard-coded product name").  The library contains no product name: the
// host fills this structure, from its build settings, and gives it to the
// library.

#ifndef OAUTH2_IDENTITY_H
#define OAUTH2_IDENTITY_H

#include <string>

namespace oauth2
{

struct Identity
{
	// Short lowercase identifier ("mulberry"): configuration folders
	// (${sysconfdir}/<id>/ and ~/.config/<id>/), prefix of keyring entries
	// and of environment variables.  See IsValidIdentityId().
	std::string id;

	// Name shown to the user ("Mulberry"): dialogs, the browser's "you can
	// close this window" page, log lines
	std::string name;

	// Version of the host application
	std::string version;
};

// True if the identifier can be used as a folder name and a prefix: it
// starts with a lowercase letter or a digit and contains only a-z, 0-9,
// '.', '_' and '-' (the rule enforced by configure --with-app-id).
bool IsValidIdentityId(const std::string& id);

} // namespace oauth2

#endif
