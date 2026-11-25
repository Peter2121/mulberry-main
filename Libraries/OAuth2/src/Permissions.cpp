/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Permissions.h"

#include "oauth2/Paths.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oauth2
{

namespace
{

std::string ModeText(mode_t mode)
{
	char text[8];
	std::snprintf(text, sizeof(text), "%04o", static_cast<unsigned>(mode & 07777));
	return text;
}

std::string ParentDirectory(const std::string& path)
{
	const size_t slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return ".";
	if (slash == 0)
		return "/";
	return path.substr(0, slash);
}

// Canonical path, or empty
std::string RealPath(const std::string& path)
{
	char* resolved = realpath(path.c_str(), nullptr);
	if (!resolved)
		return std::string();
	std::string result(resolved);
	std::free(resolved);
	return result;
}

bool IsInside(const std::string& path, const std::string& folder)
{
	if (folder.empty())
		return false;
	if (folder == "/")
		return true;
	return path.compare(0, folder.size(), folder) == 0 &&
		(path.size() == folder.size() || path[folder.size()] == '/');
}

void Add(std::vector<PermissionIssue>& issues, const std::string& path, const std::string& problem,
	bool fixable, const std::string& advice)
{
	PermissionIssue issue;
	issue.path = path;
	issue.problem = problem;
	issue.fixable = fixable;
	issue.advice = advice;
	issues.push_back(issue);
}

void CheckUser(const std::string& path, const struct stat& link, const PermissionContext& context,
	std::vector<PermissionIssue>& issues)
{
	const bool symlink = S_ISLNK(link.st_mode);
	if (symlink)
	{
		const std::string target = RealPath(path);
		const std::string home = RealPath(context.home);
		if (target.empty() || !IsInside(target, home))
			Add(issues, path, "a symbolic link pointing outside your home folder" +
				(target.empty() ? std::string() : " (" + target + ")"), false,
				"replace the link by a copy of the file: cp \"" + path + "\" \"" + path +
				".new\" && mv \"" + path + ".new\" \"" + path + "\" && chmod 600 \"" + path + "\"");
	}

	struct stat file;
	if (stat(path.c_str(), &file) != 0)
		return;

	if (!S_ISREG(file.st_mode))
	{
		Add(issues, path, "not a regular file", false, "replace it by a regular file");
		return;
	}
	const bool ownFile = file.st_uid == context.user;
	if (!ownFile)
		Add(issues, path, "not owned by you (owner uid " + std::to_string(file.st_uid) + ")", false,
			"copy it to a new file of your own, then: chmod 600 \"" + path + "\"");
	if ((file.st_mode & 077) != 0)
		Add(issues, path, "readable or writable by group or others (mode " + ModeText(file.st_mode) +
			")", ownFile && !symlink, "chmod 600 \"" + path + "\"");

	const std::string folder = ParentDirectory(path);
	struct stat dir;
	if (stat(folder.c_str(), &dir) == 0 && (dir.st_mode & 022) != 0)
		Add(issues, folder, "folder writable by group or others (mode " + ModeText(dir.st_mode) + ")",
			dir.st_uid == context.user && !symlink, "chmod 700 \"" + folder + "\"");
}

void CheckSystem(const std::string& path, const PermissionContext& context,
	std::vector<PermissionIssue>& issues)
{
	struct stat file;
	if (stat(path.c_str(), &file) != 0)
		return;
	if (!S_ISREG(file.st_mode))
	{
		Add(issues, path, "not a regular file", false, "reinstall the application");
		return;
	}
	if (file.st_uid != context.system_owner)
		Add(issues, path, "not owned by root (owner uid " + std::to_string(file.st_uid) + ")", false,
			"as root: chown root \"" + path + "\"");
	if ((file.st_mode & 022) != 0)
		Add(issues, path, "writable by group or others (mode " + ModeText(file.st_mode) + ")", false,
			"as root: chmod go-w \"" + path + "\"");

	const std::string folder = ParentDirectory(path);
	struct stat dir;
	if (stat(folder.c_str(), &dir) == 0 && (dir.st_mode & 022) != 0)
		Add(issues, folder, "folder writable by group or others (mode " + ModeText(dir.st_mode) + ")",
			false, "as root: chmod go-w \"" + folder + "\"");
}

} // namespace

PermissionContext::PermissionContext()
	: user(geteuid()), system_owner(0), home(HomeDirectory())
{
}

std::vector<PermissionIssue> CheckPermissions(const std::string& path, FileRole role,
	const PermissionContext& context)
{
	std::vector<PermissionIssue> issues;
	struct stat link;
	if (lstat(path.c_str(), &link) != 0)
		return issues;
	if (role == FileRole::User)
		CheckUser(path, link, context, issues);
	else
		CheckSystem(path, context, issues);
	return issues;
}

bool FixPermissions(const std::string& path, std::string& error, const PermissionContext& context)
{
	struct stat link;
	if (lstat(path.c_str(), &link) != 0)
	{
		if (errno == ENOENT)
			return true;
		error = path + ": " + std::strerror(errno);
		return false;
	}
	if (S_ISLNK(link.st_mode))
	{
		error = path + ": a symbolic link, not changed";
		return false;
	}
	if (!S_ISREG(link.st_mode))
	{
		error = path + ": not a regular file, not changed";
		return false;
	}
	if (link.st_uid != context.user)
	{
		error = path + ": owned by another user, not changed";
		return false;
	}

	const std::string folder = ParentDirectory(path);
	struct stat dir;
	if (stat(folder.c_str(), &dir) != 0)
	{
		error = folder + ": " + std::strerror(errno);
		return false;
	}
	if (dir.st_uid != context.user && (dir.st_mode & 022) != 0)
	{
		error = folder + ": folder owned by another user, not changed";
		return false;
	}

	if ((link.st_mode & 07777) != 0600 && (link.st_mode & 07777) != 0400 &&
		chmod(path.c_str(), 0600) != 0)
	{
		error = path + ": " + std::strerror(errno);
		return false;
	}
	if (dir.st_uid == context.user && (dir.st_mode & 022) != 0 && chmod(folder.c_str(), 0700) != 0)
	{
		error = folder + ": " + std::strerror(errno);
		return false;
	}
	return true;
}

} // namespace oauth2
