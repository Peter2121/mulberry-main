/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// htmlview::ImageCache: the remote images already downloaded, in memory, by
// URL (their encoded data, as received), so that a message shown again shows
// them without network access.  Limited in size: when an image does not fit,
// the least recently used ones are removed first.  Nothing is written to
// disk.  Not thread-safe (used from the user interface's thread).

#ifndef HTMLVIEW_IMAGECACHE_H
#define HTMLVIEW_IMAGECACHE_H

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>

namespace htmlview
{

class ImageCache
{
public:
	// TODO(remote-image-cache-size): make the size a preference (see
	// LITEHTML-VIEWER-PLAN.md, stage 5, remote images)
	static const size_t cDefaultMaxBytes = 128 * 1024 * 1024;

	explicit ImageCache(size_t max_bytes = cDefaultMaxBytes);

	ImageCache(const ImageCache&) = delete;
	ImageCache& operator=(const ImageCache&) = delete;

	// The data of an image, if cached; it becomes the most recently used
	bool	Get(const std::string& url, std::string& data);

	// Whether an image is cached (does not change the order)
	bool	Contains(const std::string& url) const;

	// Stores an image (replacing one with the same URL), removing the least
	// recently used ones until it fits; an image larger than the whole cache
	// is not stored
	void	Put(const std::string& url, const std::string& data);

	// A smaller size removes the least recently used images at once
	void	SetMaxBytes(size_t max_bytes);
	size_t	GetMaxBytes() const { return mMaxBytes; }

	size_t	GetBytes() const { return mBytes; }			// data and URLs
	size_t	GetCount() const { return mEntries.size(); }
	void	Clear();

private:
	struct Entry
	{
		std::string	url;
		std::string	data;
	};
	typedef std::list<Entry> EntryList;

	size_t		mMaxBytes;
	size_t		mBytes = 0;
	EntryList	mEntries;									// most recently used first
	std::unordered_map<std::string, EntryList::iterator> mIndex;

	static size_t	Size(const Entry& entry) { return entry.url.size() + entry.data.size(); }
	void	Remove(EntryList::iterator entry);
	void	Shrink(size_t max_bytes);
};

}	// namespace htmlview

#endif
