/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Redact.h"

#include "oauth2/Encoding.h"

#include <algorithm>

namespace oauth2
{

std::string Redact(const std::string& secret)
{
	return "<redacted " + std::to_string(secret.size()) +
		(secret.size() == 1 ? " byte>" : " bytes>");
}

const std::vector<std::string>& SecretFieldNames()
{
	static const std::vector<std::string> sNames = {
		"access_token", "refresh_token", "id_token", "code", "code_verifier",
		"client_secret", "device_code", "assertion", "password"
	};
	return sNames;
}

std::string RedactParams(const std::string& query, const std::vector<std::string>& names)
{
	std::string result;
	result.reserve(query.size());
	size_t start = 0;
	while (start <= query.size())
	{
		size_t end = query.find('&', start);
		if (end == std::string::npos)
			end = query.size();
		const std::string part = query.substr(start, end - start);
		const size_t equals = part.find('=');

		std::string name;
		const bool secret = equals != std::string::npos &&
			PercentDecode(part.substr(0, equals), name, true) &&
			std::find(names.begin(), names.end(), name) != names.end();
		if (secret)
			result += part.substr(0, equals + 1) + Redact(part.substr(equals + 1));
		else
			result += part;

		if (end < query.size())
			result += '&';
		start = end + 1;
	}
	return result;
}

} // namespace oauth2
