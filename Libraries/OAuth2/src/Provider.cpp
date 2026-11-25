/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Provider.h"

#include <algorithm>

namespace oauth2
{

const char* const cScopeAlways = "always";
const char* const cScopeMail = "mail";
const char* const cScopeCalendar = "calendar";
const char* const cScopeContacts = "contacts";

std::string Provider::ResolveEndpoint(const std::string& endpoint) const
{
	static const std::string cPlaceholder = "{tenant}";
	std::string result = endpoint;
	size_t pos = 0;
	while ((pos = result.find(cPlaceholder, pos)) != std::string::npos)
	{
		result.replace(pos, cPlaceholder.size(), tenant);
		pos += tenant.size();
	}
	return result;
}

std::vector<std::string> Provider::ScopesFor(const std::vector<std::string>& groups) const
{
	std::vector<std::string> result;
	auto add = [&](const std::string& group)
	{
		const auto found = scopes.find(group);
		if (found == scopes.end())
			return;
		for (const std::string& scope : found->second)
		{
			if (std::find(result.begin(), result.end(), scope) == result.end())
				result.push_back(scope);
		}
	};
	add(cScopeAlways);
	for (const std::string& group : groups)
		add(group);
	return result;
}

Provider WithAccountSettings(const Provider& provider, const AccountSettings& settings)
{
	Provider result = provider;
	if (!settings.client_id.empty())
		result.client_id = settings.client_id;
	if (!settings.client_secret.empty())
		result.client_secret = settings.client_secret;
	if (!settings.tenant.empty())
		result.tenant = settings.tenant;
	return result;
}

} // namespace oauth2
