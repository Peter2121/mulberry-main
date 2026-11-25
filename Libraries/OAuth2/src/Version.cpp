/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Version.h"

#include <curl/curl.h>

namespace oauth2
{

const char* LibraryVersion()
{
	return "0.1.0";
}

std::string CurlVersion()
{
	const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
	return (info && info->version) ? info->version : "";
}

std::string CurlTlsBackend()
{
	const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
	return (info && info->ssl_version) ? info->ssl_version : "";
}

} // namespace oauth2
