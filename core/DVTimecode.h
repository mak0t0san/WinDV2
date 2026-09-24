// DVTimecode.h : recording date/time from the subcode packs of a raw DV frame
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <span>

namespace windv {

inline constexpr std::size_t kDVFrameSizePAL = 144000;  // 12 DIF sequences
inline constexpr std::size_t kDVFrameSizeNTSC = 120000; // 10 DIF sequences

// Returns the camcorder's recording timestamp (local time) stored in the frame's
// SSYB packs 0x62 (date) and 0x63 (time), or nullopt when the frame is not a
// full DV frame, the packs are missing, or they hold invalid BCD values.
std::optional<std::time_t> GetDVRecordingTime(std::span<const std::uint8_t> frame);

} // namespace windv
