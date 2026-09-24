// CommandLine.cpp : parsing WinDV's command line

#include "CommandLine.h"

namespace windv {

namespace {

constexpr std::int64_t kUnitsPerSecond = 10'000'000;
constexpr std::size_t kMaxFieldDigits = 9;

// Parses a run of decimal digits. Empty or over-long fields are rejected.
std::optional<std::int64_t> ParseField(std::wstring_view text)
{
	if (text.empty() || text.size() > kMaxFieldDigits) {
		return std::nullopt;
	}
	std::int64_t value = 0;
	for (wchar_t c : text) {
		if (c < L'0' || c > L'9') {
			return std::nullopt;
		}
		value = value * 10 + (c - L'0');
	}
	return value;
}

} // namespace

std::optional<std::int64_t> ParseDuration(std::wstring_view text)
{
	std::int64_t fraction = 0;
	if (const std::size_t dot = text.find(L'.'); dot != std::wstring_view::npos) {
		const std::wstring_view digits = text.substr(dot + 1);
		// Digits past 100 ns resolution are accepted and ignored.
		std::int64_t weight = kUnitsPerSecond / 10;
		for (wchar_t c : digits) {
			if (c < L'0' || c > L'9') {
				return std::nullopt;
			}
			fraction += weight * (c - L'0');
			weight /= 10;
		}
		text = text.substr(0, dot);
	}

	std::int64_t seconds = 0;
	int fields = 0;
	while (true) {
		const std::size_t colon = text.find(L':');
		const auto field = ParseField(text.substr(0, colon));
		if (!field || ++fields > 3) {
			return std::nullopt;
		}
		seconds = seconds * 60 + *field;
		if (colon == std::wstring_view::npos) {
			break;
		}
		text = text.substr(colon + 1);
	}
	return seconds * kUnitsPerSecond + fraction;
}

std::optional<CommandLine> ParseCommandLine(std::span<const std::wstring> args)
{
	CommandLine result;
	if (args.empty()) {
		return result;
	}

	const std::wstring& command = args[0];
	args = args.subspan(1);
	if (!args.empty() && args[0] == L"-exit") {
		result.exitOnFinish = true;
		args = args.subspan(1);
	}

	if (command == L"capture") {
		if (args.size() != 2) {
			return std::nullopt;
		}
		const auto duration = ParseDuration(args[0]);
		if (!duration) {
			return std::nullopt;
		}
		result.mode = CommandLine::Mode::Capture;
		result.duration = *duration;
		result.captureFile = args[1];
		return result;
	}

	if (command == L"record") {
		if (args.empty()) {
			return std::nullopt;
		}
		result.mode = CommandLine::Mode::Record;
		result.recordFiles.assign(args.begin(), args.end());
		return result;
	}

	return std::nullopt;
}

} // namespace windv
