// TimeFormat.h : strftime-style formatting that is safe for user-typed formats
#pragma once

#include <ctime>
#include <string>
#include <string_view>

namespace windv {

// True if every '%' in the format introduces a conversion the UCRT accepts.
// wcsftime treats anything else as an invalid parameter and terminates the
// process, so formats typed by the user must be checked first.
bool IsValidTimeFormat(std::wstring_view format);

// Formats a timestamp in local time. Returns an empty string if the format is
// invalid or the timestamp cannot be represented.
std::wstring FormatTime(std::wstring_view format, std::time_t time);

} // namespace windv
