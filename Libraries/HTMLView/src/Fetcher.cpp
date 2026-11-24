/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// htmlview::Fetcher with libcurl's multi interface, see Fetcher.h

#include "htmlview/Fetcher.h"

#include <curl/curl.h>

#include <cctype>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace htmlview
{

namespace
{

// The image formats Images.cpp decodes (no SVG, no AVIF)
#ifdef HTMLVIEW_WEBP
const char* const cAccept = "Accept: image/webp,image/png,image/jpeg,image/gif;q=0.9,image/*;q=0.5";
#else
const char* const cAccept = "Accept: image/png,image/jpeg,image/gif;q=0.9,image/*;q=0.5";
#endif

bool IsHTTP(const std::string& url)
{
	std::string scheme;
	for (char c : url)
	{
		if (c == ':')
			break;
		scheme += (char) std::tolower((unsigned char) c);
	}
	return (scheme == "http") || (scheme == "https");
}

void GlobalInit()
{
	static std::once_flag once;
	std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

}	// namespace

struct Fetcher::Impl
{
	struct Transfer
	{
		Impl*		impl = nullptr;
		CURL*		easy = nullptr;
		std::string	url;
		std::string	data;
		bool		too_large = false;
	};

	FetchHost*					mHost;
	FetchSettings				mSettings;
	CURLM*						mMulti = nullptr;
	curl_slist*					mHeaders = nullptr;
	std::map<CURL*, Transfer*>	mTransfers;
	std::set<std::string>		mSeen;			// URLs added
	size_t						mStarted = 0;
	size_t						mTotalBytes = 0;
	bool						mCancelled = false;

	Impl(FetchHost* host, const FetchSettings& settings)
		: mHost(host), mSettings(settings)
	{
		GlobalInit();
		mMulti = curl_multi_init();
		curl_multi_setopt(mMulti, CURLMOPT_MAX_HOST_CONNECTIONS, mSettings.max_host_connections);
		curl_multi_setopt(mMulti, CURLMOPT_MAX_TOTAL_CONNECTIONS, mSettings.max_connections);
		mHeaders = curl_slist_append(mHeaders, cAccept);
	}

	~Impl()
	{
		RemoveAll();
		curl_multi_cleanup(mMulti);
		curl_slist_free_all(mHeaders);
	}

	void RemoveAll()
	{
		for (auto& item : mTransfers)
		{
			curl_multi_remove_handle(mMulti, item.first);
			curl_easy_cleanup(item.first);
			delete item.second;
		}
		mTransfers.clear();
	}

	static size_t Write(char* buffer, size_t size, size_t count, void* user)
	{
		auto* transfer = static_cast<Transfer*>(user);
		Impl* impl = transfer->impl;
		size_t bytes = size * count;
		// Stops a download over the limits: libcurl then reports a write error
		if ((transfer->data.size() + bytes > impl->mSettings.max_bytes) ||
			(impl->mTotalBytes + bytes > impl->mSettings.max_total_bytes))
		{
			transfer->too_large = true;
			return 0;
		}
		transfer->data.append(buffer, bytes);
		impl->mTotalBytes += bytes;
		return bytes;
	}

	static int Debug(CURL*, curl_infotype type, char* text, size_t size, void* user)
	{
		auto* transfer = static_cast<Transfer*>(user);
		const char* prefix = nullptr;
		switch (type)
		{
		case CURLINFO_TEXT:
			prefix = "* ";
			break;
		case CURLINFO_HEADER_OUT:
			prefix = "> ";
			break;
		case CURLINFO_HEADER_IN:
			prefix = "< ";
			break;
		default:
			// Not the data (images, TLS records)
			return 0;
		}

		// One log entry per call, each line prefixed as curl -v does
		std::string entry;
		std::string line;
		for (size_t i = 0; i < size; i++)
		{
			char c = text[i];
			if (c == '\r')
				continue;
			if (c == '\n')
			{
				entry += prefix + line + "\n";
				line.clear();
			}
			else
				line += c;
		}
		if (!line.empty())
			entry += prefix + line + "\n";
		if (!entry.empty())
		{
			entry.pop_back();
			transfer->impl->mHost->Log(transfer->url + "\n" + entry);
		}
		return 0;
	}

	void Fail(const std::string& url, const std::string& error)
	{
		if (mHost)
			mHost->FetchDone(url, false, std::string(), error);
	}

	void Add(const std::string& url)
	{
		if (mSeen.count(url))
			return;
		mSeen.insert(url);

		if (!IsHTTP(url))
		{
			Fail(url, "not an http or https URL");
			return;
		}
		if (mStarted >= mSettings.max_requests)
		{
			Fail(url, "too many images");
			return;
		}

		auto* transfer = new Transfer;
		transfer->impl = this;
		transfer->url = url;
		CURL* easy = curl_easy_init();
		transfer->easy = easy;

		curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
		curl_easy_setopt(easy, CURLOPT_PRIVATE, transfer);
		curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);

		// Protocols, also after redirects
#if LIBCURL_VERSION_NUM >= 0x075500
		curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
		curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
		curl_easy_setopt(easy, CURLOPT_PROTOCOLS, (long) (CURLPROTO_HTTP | CURLPROTO_HTTPS));
		curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS, (long) (CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
		curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(easy, CURLOPT_MAXREDIRS, mSettings.max_redirects);

		// Nothing about the reader: no cookies (the cookie engine is off
		// unless asked for), no Referer, no credentials
		curl_easy_setopt(easy, CURLOPT_AUTOREFERER, 0L);
		curl_easy_setopt(easy, CURLOPT_NETRC, (long) CURL_NETRC_IGNORED);
		curl_easy_setopt(easy, CURLOPT_DISALLOW_USERNAME_IN_URL, 1L);
		curl_easy_setopt(easy, CURLOPT_USERAGENT, mSettings.user_agent.c_str());
		curl_easy_setopt(easy, CURLOPT_HTTPHEADER, mHeaders);
		curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");

		// Limits
		curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, mSettings.connect_timeout);
		curl_easy_setopt(easy, CURLOPT_TIMEOUT, mSettings.timeout);
		curl_easy_setopt(easy, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t) mSettings.max_bytes);
		curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &Impl::Write);
		curl_easy_setopt(easy, CURLOPT_WRITEDATA, transfer);

		// HTTP/2: several images over one connection
		curl_easy_setopt(easy, CURLOPT_PIPEWAIT, 1L);

		if (mHost && mHost->LogEnabled())
		{
			curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
			curl_easy_setopt(easy, CURLOPT_DEBUGFUNCTION, &Impl::Debug);
			curl_easy_setopt(easy, CURLOPT_DEBUGDATA, transfer);
		}

		mTransfers[easy] = transfer;
		mStarted++;
		curl_multi_add_handle(mMulti, easy);
	}

	bool Pump()
	{
		mCancelled = false;
		int running = 0;
		curl_multi_perform(mMulti, &running);

		// Collect first, then report: the host may Cancel() from FetchDone()
		struct Done
		{
			std::string	url;
			bool		ok;
			std::string	data;
			std::string	error;
		};
		std::vector<Done> done;
		int left = 0;
		while (CURLMsg* msg = curl_multi_info_read(mMulti, &left))
		{
			if (msg->msg != CURLMSG_DONE)
				continue;
			CURL* easy = msg->easy_handle;
			auto found = mTransfers.find(easy);
			if (found == mTransfers.end())
				continue;
			Transfer* transfer = found->second;

			Done result;
			result.url = transfer->url;
			result.ok = (msg->data.result == CURLE_OK) && !transfer->data.empty();
			if (result.ok)
				result.data.swap(transfer->data);
			else if (transfer->too_large || (msg->data.result == CURLE_FILESIZE_EXCEEDED))
				result.error = "larger than the size limit";
			else if (msg->data.result == CURLE_OK)
				result.error = "empty response";
			else if (msg->data.result == CURLE_HTTP_RETURNED_ERROR)
			{
				long code = 0;
				curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
				result.error = "HTTP " + std::to_string(code);
			}
			else
				result.error = curl_easy_strerror(msg->data.result);
			done.push_back(std::move(result));

			curl_multi_remove_handle(mMulti, easy);
			curl_easy_cleanup(easy);
			delete transfer;
			mTransfers.erase(found);
		}

		for (const auto& result : done)
		{
			if (mCancelled)
				break;
			if (mHost)
				mHost->FetchDone(result.url, result.ok, result.data, result.error);
		}
		return !mTransfers.empty();
	}

	void Cancel()
	{
		RemoveAll();
		mCancelled = true;
	}
};

Fetcher::Fetcher(FetchHost* host, const FetchSettings& settings)
	: mImpl(new Impl(host, settings))
{
}

Fetcher::~Fetcher()
{
}

void Fetcher::Add(const std::string& url)
{
	mImpl->Add(url);
}

bool Fetcher::Pump()
{
	return mImpl->Pump();
}

void Fetcher::Wait(int timeout_ms)
{
	int events = 0;
	curl_multi_poll(mImpl->mMulti, nullptr, 0, timeout_ms, &events);
}

void Fetcher::Cancel()
{
	mImpl->Cancel();
}

size_t Fetcher::GetRunning() const
{
	return mImpl->mTransfers.size();
}

size_t Fetcher::GetStarted() const
{
	return mImpl->mStarted;
}

}	// namespace htmlview
