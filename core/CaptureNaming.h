// CaptureNaming.h : choosing the next free capture filename
#pragma once

#include <span>
#include <string>
#include <string_view>

namespace windv {

// Joins the user's base path and the formatted date/time: "base.date", or just
// "base" when the date part is empty.
std::wstring CaptureStem(std::wstring_view base, std::wstring_view date);

// Returns the glob pattern whose matches must be passed to NextCaptureFilename.
std::wstring CaptureSearchPattern(std::wstring_view stem);

// Picks the next filename for a capture with the given stem (which may include
// a directory). existingNames are the file names (no directory) already in that
// directory that match CaptureSearchPattern; anything unrelated is ignored.
//
// Captures are numbered "stem.NN.avi" using at least ndigits digits, continuing
// after the highest existing number. With ndigits == 0 the first capture is
// "stem.avi" and numbering only starts once that exists.
std::wstring NextCaptureFilename(std::wstring_view stem, int ndigits, std::span<const std::wstring> existingNames);

// Reduces a capture filename to its base: "D:\dv\tape.04-07-15.00.avi" gives
// "D:\dv\tape". Only the file name part is cut, so dots in folders are kept.
std::wstring CaptureBaseFromFilename(std::wstring_view file);

} // namespace windv
