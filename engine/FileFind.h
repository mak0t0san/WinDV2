// FileFind.h : wildcard file search (FindFirstFile semantics, like MFC's CFileFind)
#pragma once

#include "DShowBase.h"

struct FoundFile {
	std::wstring name; // file name only
	std::wstring path; // the pattern's directory part + name
	bool isDirectory = false;
};

// Everything matching pattern, which may have wildcards in its last component.
// "." and ".." are skipped. An empty result means nothing matched.
std::vector<FoundFile> FindFiles(const std::wstring& pattern);
