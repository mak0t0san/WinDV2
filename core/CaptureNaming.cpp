// CaptureNaming.cpp : choosing the next free capture filename

#include "CaptureNaming.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace windv {

namespace {

constexpr std::wstring_view kExtension = L".avi";
constexpr std::size_t kMaxDigits = 9; // keeps the parsed number inside an int

std::wstring_view FileNamePart(std::wstring_view path)
{
	const std::size_t pos = path.find_last_of(L"\\/:");
	return pos == std::wstring_view::npos ? path : path.substr(pos + 1);
}

// Windows file names compare case-insensitively.
bool EqualsNoCase(std::wstring_view a, std::wstring_view b)
{
	return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

bool IsDecimal(std::wstring_view s)
{
	return !s.empty() && std::ranges::all_of(s, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

} // namespace

std::wstring CaptureStem(std::wstring_view base, std::wstring_view date)
{
	std::wstring stem(base);
	if (!date.empty()) {
		stem += L'.';
		stem += date;
	}
	return stem;
}

std::wstring CaptureSearchPattern(std::wstring_view stem)
{
	return std::wstring(stem) + L"*" + std::wstring(kExtension);
}

std::wstring NextCaptureFilename(std::wstring_view stem, int ndigits, std::span<const std::wstring> existingNames)
{
	const std::wstring_view name = FileNamePart(stem);
	const std::wstring plainName = std::wstring(name) + std::wstring(kExtension);

	std::size_t width = static_cast<std::size_t>(std::max(ndigits, 0));
	int highest = -1;
	bool plainExists = false;

	for (const std::wstring& existing : existingNames) {
		const std::wstring_view candidate = existing;
		if (EqualsNoCase(candidate, plainName)) {
			plainExists = true;
			continue;
		}
		// "name." + digits + ".avi"
		if (candidate.size() <= name.size() + 1 + kExtension.size())
			continue;
		if (!EqualsNoCase(candidate.substr(0, name.size()), name) || candidate[name.size()] != L'.')
			continue;
		if (!EqualsNoCase(candidate.substr(candidate.size() - kExtension.size()), kExtension))
			continue;

		const std::wstring_view digits =
		    candidate.substr(name.size() + 1, candidate.size() - name.size() - 1 - kExtension.size());
		if (!IsDecimal(digits) || digits.size() > kMaxDigits || digits.size() < width)
			continue;

		const int number = std::stoi(std::wstring(digits));
		if (digits.size() > width) {
			// A wider number is always a later one, so restart the search there.
			width = digits.size();
			highest = number;
		} else {
			highest = std::max(highest, number);
		}
	}

	if (width == 0)
		return std::wstring(stem) + (plainExists ? L".0" : L"") + std::wstring(kExtension);

	wchar_t number[32];
	swprintf_s(number, L".%0*d", static_cast<int>(width), highest + 1);
	return std::wstring(stem) + number + std::wstring(kExtension);
}

} // namespace windv
