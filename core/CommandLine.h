// CommandLine.h : parsing WinDV's command line
//
//   WinDV.exe capture [-exit] <duration> <file>
//   WinDV.exe record [-exit] <file> [<file>...]
//
// <duration> is [[hh:]mm:]ss[.fraction]. A duration of 0 captures until stopped.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace windv {

struct CommandLine {
	enum class Mode { Interactive, Capture, Record };

	Mode mode = Mode::Interactive;
	bool exitOnFinish = false;
	std::int64_t duration = 0; // REFERENCE_TIME units (100 ns)
	std::wstring captureFile;
	std::vector<std::wstring> recordFiles;
};

// Parses a duration into 100 ns units, or nullopt if it is malformed.
std::optional<std::int64_t> ParseDuration(std::wstring_view text);

// args excludes the program name (argv[1..]). Returns nullopt on a usage error;
// no arguments at all gives Mode::Interactive.
std::optional<CommandLine> ParseCommandLine(std::span<const std::wstring> args);

} // namespace windv
