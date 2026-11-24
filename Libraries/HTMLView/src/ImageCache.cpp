/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "htmlview/ImageCache.h"

#include <iterator>

namespace htmlview
{

ImageCache::ImageCache(size_t max_bytes)
	: mMaxBytes(max_bytes)
{
}

bool ImageCache::Get(const std::string& url, std::string& data)
{
	auto found = mIndex.find(url);
	if (found == mIndex.end())
		return false;

	// Most recently used: to the front (the iterator stays valid)
	mEntries.splice(mEntries.begin(), mEntries, found->second);
	data = found->second->data;
	return true;
}

bool ImageCache::Contains(const std::string& url) const
{
	return mIndex.count(url) != 0;
}

void ImageCache::Put(const std::string& url, const std::string& data)
{
	auto found = mIndex.find(url);
	if (found != mIndex.end())
		Remove(found->second);

	Entry entry;
	entry.url = url;
	entry.data = data;
	size_t size = Size(entry);
	if (size > mMaxBytes)
		return;

	Shrink(mMaxBytes - size);
	mEntries.push_front(std::move(entry));
	mIndex[url] = mEntries.begin();
	mBytes += size;
}

void ImageCache::SetMaxBytes(size_t max_bytes)
{
	mMaxBytes = max_bytes;
	Shrink(mMaxBytes);
}

void ImageCache::Clear()
{
	mEntries.clear();
	mIndex.clear();
	mBytes = 0;
}

void ImageCache::Remove(EntryList::iterator entry)
{
	mBytes -= Size(*entry);
	mIndex.erase(entry->url);
	mEntries.erase(entry);
}

// Removes the least recently used images until at most max_bytes are used
void ImageCache::Shrink(size_t max_bytes)
{
	while ((mBytes > max_bytes) && !mEntries.empty())
		Remove(std::prev(mEntries.end()));
}

}	// namespace htmlview
