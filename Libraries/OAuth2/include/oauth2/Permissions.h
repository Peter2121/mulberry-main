/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Ownership and permission checks of configuration and secret files
// (OAUTH2-PLAN.md, section 7, "File permissions").  Every finding is a
// warning: the caller shows it and continues.  The user's own files can be
// fixed; the system file only gets advice for the administrator.

#ifndef OAUTH2_PERMISSIONS_H
#define OAUTH2_PERMISSIONS_H

#include <sys/types.h>

#include <string>
#include <vector>

namespace oauth2
{

enum class FileRole
{
	// A file of the user (provider file, keyring, token files): mode 0600
	// or 0400, owned by the user, in a folder only the user can write
	User,
	// The system provider file: owned by root, writable by nobody else, in a
	// folder only root can write; readable by everyone is normal
	System
};

struct PermissionIssue
{
	std::string path;		// the file or folder concerned
	std::string problem;	// "readable by group or others (mode 0644)"
	bool fixable = false;	// FixPermissions() can repair it
	std::string advice;		// what to do by hand ("chmod 600 <file>")
};

// Who is expected to own the files.  The defaults are the real values;
// tests change them to simulate a foreign owner or a root-owned file.
struct PermissionContext
{
	PermissionContext();

	uid_t user;			// the effective user
	uid_t system_owner;	// root
	std::string home;	// the home folder (symbolic links must stay inside)
};

// Checks a file and its folder.  A missing file gives no issue (a missing
// user file is normal).
std::vector<PermissionIssue> CheckPermissions(const std::string& path, FileRole role,
	const PermissionContext& context = PermissionContext());

// Repairs a user file: the file becomes 0600 and its folder 0700.  Refused
// (false, with error set) for a symbolic link, a file or folder owned by
// someone else, or anything but a regular file.  A missing file is not an
// error.
bool FixPermissions(const std::string& path, std::string& error,
	const PermissionContext& context = PermissionContext());

} // namespace oauth2

#endif
