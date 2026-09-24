// FileFind.cpp : wildcard file search (FindFirstFile semantics, like MFC's CFileFind)

#include "DShowBase.h"
#include "FileFind.h"

std::vector<FoundFile> FindFiles(const std::wstring& pattern)
{
	std::vector<FoundFile> result;

	const std::size_t separator = pattern.find_last_of(L"\\/:");
	const std::wstring directory = separator == std::wstring::npos ? std::wstring() : pattern.substr(0, separator + 1);

	WIN32_FIND_DATAW data{};
	const HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
	if (find == INVALID_HANDLE_VALUE) {
		return result;
	}
	do {
		const std::wstring name = data.cFileName;
		if (name == L"." || name == L"..") {
			continue;
		}
		result.push_back({name, directory + name, (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
	} while (FindNextFileW(find, &data));
	FindClose(find);
	return result;
}
