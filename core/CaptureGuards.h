// CaptureGuards.h : when a capture must stop on its own - disk space and signal loss
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace windv {

// Free space kept in reserve on the capture disk. Capture refuses to start,
// or stops and finishes its file, below this: the AVI index and the rename
// still need room, and a full system disk makes Windows itself misbehave.
constexpr std::uint64_t kDiskReserveBytes = 256ull * 1024 * 1024;

inline bool IsDiskNearlyFull(std::uint64_t freeBytes)
{
	return freeBytes < kDiskReserveBytes;
}

// The folder a capture with this base name is written to, for asking about
// free space: "D:\dv\tape" gives "D:\dv\", "C:tape" gives "C:", and a bare
// "tape" gives "" (the current directory).
std::wstring CaptureDirectory(std::wstring_view fileBase);

// True for a capture device on the FireWire AV/C or IEC 61883 bus, i.e. a DV
// camcorder or deck: its device path looks like "\\?\avc#ven_...&dv#...".
// Webcams (usb#...) and virtual devices (root#...) are not.
bool IsDVDevicePath(std::wstring_view devicePath);

// Watches for the end of the DV signal, e.g. the end of the recorded part of a
// tape. It only fires after frames have arrived and then stopped for longer
// than the timeout, so a capture started before the tape rolls is left alone.
class SignalWatch {
public:
	using Clock = std::chrono::steady_clock;

	// timeout 0 turns the watch off.
	explicit SignalWatch(std::chrono::milliseconds timeout = {}) : m_timeout(timeout) {}

	void Frame(Clock::time_point now) { m_lastFrame = now; }
	void Reset() { m_lastFrame.reset(); }
	bool Lost(Clock::time_point now) const
	{
		return m_timeout.count() > 0 && m_lastFrame && now - *m_lastFrame >= m_timeout;
	}

private:
	std::chrono::milliseconds m_timeout;
	std::optional<Clock::time_point> m_lastFrame;
};

} // namespace windv
