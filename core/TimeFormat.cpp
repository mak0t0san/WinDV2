// TimeFormat.cpp : strftime-style formatting that is safe for user-typed formats

#include "TimeFormat.h"

#include <cwchar>
#include <vector>

namespace windv {

namespace {

constexpr std::wstring_view kConversions = L"aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%";
constexpr std::wstring_view kEModified = L"cCxXyY";
constexpr std::wstring_view kOModified = L"deHImMSuUVwWy";

bool Contains(std::wstring_view set, wchar_t c)
{
	return set.find(c) != std::wstring_view::npos;
}

} // namespace

bool IsValidTimeFormat(std::wstring_view format)
{
	for (std::size_t i = 0; i < format.size(); ++i) {
		if (format[i] != L'%') {
			continue;
		}
		if (++i == format.size()) {
			return false;
		}

		std::wstring_view allowed = kConversions;
		switch (format[i]) {
		case L'#': // Microsoft extension, accepted in front of any conversion
			break;
		case L'E':
			allowed = kEModified;
			break;
		case L'O':
			allowed = kOModified;
			break;
		default:
			if (!Contains(kConversions, format[i])) {
				return false;
			}
			continue;
		}
		if (++i == format.size() || !Contains(allowed, format[i])) {
			return false;
		}
	}
	return true;
}

std::wstring FormatTime(std::wstring_view format, std::time_t time)
{
	if (format.empty() || !IsValidTimeFormat(format)) {
		return {};
	}

	std::tm local{};
	if (localtime_s(&local, &time) != 0) {
		return {};
	}

	const std::wstring fmt(format);
	// wcsftime returns 0 both for "buffer too small" and for an empty result
	// (e.g. %p in a locale without AM/PM), so grow a few times and then give up.
	for (std::size_t size = 256; size <= 16384; size *= 4) {
		std::vector<wchar_t> buf(size);
		const std::size_t len = std::wcsftime(buf.data(), buf.size(), fmt.c_str(), &local);
		if (len > 0) {
			return std::wstring(buf.data(), len);
		}
	}
	return {};
}

} // namespace windv
