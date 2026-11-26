/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Authorization.h"

#include "oauth2/Pkce.h"
#include "oauth2/Provider.h"

#include <algorithm>
#include <map>

namespace oauth2
{

namespace
{

std::string JoinScopes(const std::vector<std::string>& scopes)
{
	std::string result;
	for (const std::string& scope : scopes)
		result += (result.empty() ? "" : " ") + scope;
	return result;
}

bool Contains(const std::vector<std::string>& list, const std::string& item)
{
	return std::find(list.begin(), list.end(), item) != list.end();
}

} // namespace

std::string BuildAuthorizationUrl(const Provider& provider, const AuthorizationRequest& request)
{
	Params params;
	params.emplace_back("response_type", "code");
	params.emplace_back("client_id", provider.client_id);
	params.emplace_back("redirect_uri", request.redirect_uri);
	params.emplace_back("scope", JoinScopes(request.scopes));
	params.emplace_back("state", request.state);
	if (provider.pkce && !request.code_challenge.empty())
	{
		params.emplace_back("code_challenge", request.code_challenge);
		params.emplace_back("code_challenge_method", Pkce::cMethod);
	}
	if (!request.login_hint.empty())
		params.emplace_back("login_hint", request.login_hint);

	// The provider's extra parameters (a sorted map), as changed by the
	// overrides; the ones set above are never replaced
	std::map<std::string, std::string> extra = provider.authorization_params;
	for (const auto& item : request.overrides)
	{
		const bool fixed = std::any_of(params.begin(), params.end(),
			[&item](const std::pair<std::string, std::string>& p) { return p.first == item.first; });
		if (fixed)
			continue;
		if (item.second.empty())
			extra.erase(item.first);
		else
			extra[item.first] = item.second;
	}
	for (const auto& item : extra)
		params.emplace_back(item.first, item.second);

	const std::string endpoint = provider.ResolveEndpoint(provider.authorization_endpoint);
	return endpoint + (endpoint.find('?') == std::string::npos ? "?" : "&") + BuildQuery(params);
}

std::vector<std::string> MissingScopes(const Provider& provider,
	const std::vector<std::string>& requested, bool scope_given,
	const std::vector<std::string>& granted)
{
	std::vector<std::string> missing;
	if (!scope_given)
		return missing;
	const auto always = provider.scopes.find(cScopeAlways);
	for (const std::string& scope : requested)
	{
		if (always != provider.scopes.end() && Contains(always->second, scope))
			continue;
		if (!Contains(granted, scope) && !Contains(missing, scope))
			missing.push_back(scope);
	}
	return missing;
}

std::vector<std::string> RefreshScopes(const Provider& provider,
	const std::vector<std::string>& requested, bool scope_given,
	const std::vector<std::string>& granted)
{
	const std::vector<std::string> missing = MissingScopes(provider, requested, scope_given, granted);
	std::vector<std::string> result;
	for (const std::string& scope : requested)
		if (!Contains(missing, scope) && !Contains(result, scope))
			result.push_back(scope);
	return result;
}

} // namespace oauth2
