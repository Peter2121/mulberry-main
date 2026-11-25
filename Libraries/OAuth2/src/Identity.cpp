/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Identity.h"

namespace oauth2
{

namespace
{

bool IsLowerAlnum(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

} // namespace

bool IsValidIdentityId(const std::string& id)
{
	if (id.empty() || !IsLowerAlnum(id[0]))
		return false;
	for (char c : id)
	{
		if (!IsLowerAlnum(c) && c != '.' && c != '_' && c != '-')
			return false;
	}
	return true;
}

} // namespace oauth2
