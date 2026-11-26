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

const char* const cClientIdUuid = "uuid";

const char* const cClientCheckToken = "token";
const char* const cClientCheckDevice = "device_authorization";

namespace
{

// xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx, hexadecimal digits of either case
bool IsUuid(const std::string& text)
{
	if (text.size() != 36)
		return false;
	for (size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];
		if (i == 8 || i == 13 || i == 18 || i == 23)
		{
			if (c != '-')
				return false;
		}
		else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return false;
	}
	return true;
}

} // namespace

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

bool CheckClientIdFormat(const Provider& provider, std::string& error)
{
	if (provider.client_id.empty() || provider.client_id_format != cClientIdUuid ||
		IsUuid(provider.client_id))
		return true;
	// The value is not repeated: a secret pasted into the wrong field would
	// be shown
	error = "the client ID (" + std::to_string(provider.client_id.size()) + " characters) is not in " +
		provider.name + "'s form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (hexadecimal digits)";
	return false;
}

} // namespace oauth2
