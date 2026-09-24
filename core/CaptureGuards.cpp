// CaptureGuards.cpp : when a capture must stop on its own - disk space and signal loss

#include "CaptureGuards.h"

#include <cwchar>

namespace windv {

std::wstring CaptureDirectory(std::wstring_view fileBase)
{
	const std::size_t separator = fileBase.find_last_of(L"\\/:");
	if (separator == std::wstring_view::npos) {
		return {};
	}
	return std::wstring(fileBase.substr(0, separator + 1));
}

bool IsDVDevicePath(std::wstring_view devicePath)
{
	// Skip "\\?\" (or "##?#" as it appears in the registry), then compare the
	// bus name, case-insensitively.
	if (devicePath.size() >= 4 && (devicePath.substr(0, 4) == L"\\\\?\\" || devicePath.substr(0, 4) == L"##?#")) {
		devicePath.remove_prefix(4);
	}
	for (std::wstring_view bus : {std::wstring_view(L"avc#"), std::wstring_view(L"61883#")}) {
		if (devicePath.size() >= bus.size() && _wcsnicmp(devicePath.data(), bus.data(), bus.size()) == 0) {
			return true;
		}
	}
	return false;
}

} // namespace windv
