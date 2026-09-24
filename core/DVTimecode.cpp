// DVTimecode.cpp : recording date/time from the subcode packs of a raw DV frame

#include "DVTimecode.h"

#include <array>

namespace windv {

namespace {

using Pack = std::array<std::uint8_t, 5>;

// DV frame layout (IEC 61834): DIF sequences of 150 blocks x 80 bytes. Blocks 1
// and 2 of each sequence are subcode blocks; after a 3-byte block header each
// holds 6 SSYB packets of 8 bytes (3-byte header + 5-byte pack).
constexpr std::size_t kDIFBlockSize = 80;
constexpr std::size_t kDIFSequenceSize = 150 * kDIFBlockSize;

std::optional<Pack> FindSSYBPack(std::span<const std::uint8_t> frame, std::uint8_t packId)
{
	const std::size_t seqCount = frame.size() >= kDVFrameSizePAL ? 12 : 10;

	for (std::size_t seq = 0; seq < seqCount; ++seq) {
		for (std::size_t block = 1; block <= 2; ++block) {
			for (std::size_t packet = 0; packet < 6; ++packet) {
				const std::size_t offset = seq * kDIFSequenceSize + block * kDIFBlockSize + 3 + packet * 8 + 3;
				if (offset + 5 > frame.size()) {
					return std::nullopt;
				}
				if (frame[offset] == packId) {
					Pack pack{};
					for (std::size_t i = 0; i < pack.size(); ++i) {
						pack[i] = frame[offset + i];
					}
					return pack;
				}
			}
		}
	}
	return std::nullopt;
}

// Decodes a BCD byte after masking off the flag bits above the tens digit.
// Returns -1 if either digit is not a decimal digit.
int DecodeBCD(std::uint8_t value, std::uint8_t tensMask)
{
	const int units = value & 0x0f;
	const int tens = (value >> 4) & tensMask;
	if (units > 9 || tens > 9) {
		return -1;
	}
	return tens * 10 + units;
}

} // namespace

std::optional<std::time_t> GetDVRecordingTime(std::span<const std::uint8_t> frame)
{
	if (frame.size() != kDVFrameSizePAL && frame.size() != kDVFrameSizeNTSC) {
		return std::nullopt;
	}

	const auto date = FindSSYBPack(frame, 0x62);
	if (!date) {
		return std::nullopt;
	}
	const auto time = FindSSYBPack(frame, 0x63);
	if (!time) {
		return std::nullopt;
	}

	const int day = DecodeBCD((*date)[2], 0x3);
	const int month = DecodeBCD((*date)[3], 0x1);
	int year = DecodeBCD((*date)[4], 0xf);
	const int sec = DecodeBCD((*time)[2], 0x7);
	const int min = DecodeBCD((*time)[3], 0x7);
	const int hour = DecodeBCD((*time)[4], 0x3);

	if (day < 1 || day > 31 || month < 1 || month > 12 || year < 0 || sec < 0 || sec > 59 || min < 0 || min > 59 ||
	    hour < 0 || hour > 23) {
		return std::nullopt;
	}

	year += year < 50 ? 2000 : 1900;

	std::tm recDate{};
	recDate.tm_sec = sec;
	recDate.tm_min = min;
	recDate.tm_hour = hour;
	recDate.tm_mday = day;
	recDate.tm_mon = month - 1;
	recDate.tm_year = year - 1900;
	recDate.tm_isdst = -1;

	const std::time_t result = std::mktime(&recDate);
	if (result == static_cast<std::time_t>(-1)) {
		return std::nullopt;
	}
	return result;
}

} // namespace windv
