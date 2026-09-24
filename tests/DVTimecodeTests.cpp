#include <doctest.h>

#include "DVTimecode.h"

#include <vector>

using namespace windv;

namespace {

// Offset of SSYB pack `packet` in subcode block `block` (1 or 2) of DIF sequence `seq`.
std::size_t PackOffset(std::size_t seq, std::size_t block, std::size_t packet)
{
	return seq * 150 * 80 + block * 80 + 3 + packet * 8 + 3;
}

void PutPack(std::vector<std::uint8_t>& frame, std::size_t offset, std::uint8_t id, std::uint8_t b2, std::uint8_t b3,
             std::uint8_t b4)
{
	frame[offset] = id;
	frame[offset + 1] = 0xff;
	frame[offset + 2] = b2;
	frame[offset + 3] = b3;
	frame[offset + 4] = b4;
}

// A frame (filled with 0xff like unused DV subcode) holding 2004-07-15 13:45:30.
std::vector<std::uint8_t> MakeFrame(std::size_t size, std::size_t seq = 0)
{
	std::vector<std::uint8_t> frame(size, 0xff);
	PutPack(frame, PackOffset(seq, 1, 2), 0x62, 0x15, 0x07, 0x04); // day, month, year (BCD)
	PutPack(frame, PackOffset(seq, 2, 4), 0x63, 0x30, 0x45, 0x13); // sec, min, hour (BCD)
	return frame;
}

std::tm ToLocal(std::time_t t)
{
	std::tm tm{};
	REQUIRE(localtime_s(&tm, &t) == 0);
	return tm;
}

} // namespace

TEST_CASE("DV recording time is decoded from PAL and NTSC frames")
{
	for (std::size_t size : {kDVFrameSizePAL, kDVFrameSizeNTSC}) {
		CAPTURE(size);
		const auto frame = MakeFrame(size);
		const auto t = GetDVRecordingTime(frame);
		REQUIRE(t.has_value());
		const std::tm tm = ToLocal(*t);
		CHECK(tm.tm_year == 104);
		CHECK(tm.tm_mon == 6);
		CHECK(tm.tm_mday == 15);
		CHECK(tm.tm_hour == 13);
		CHECK(tm.tm_min == 45);
		CHECK(tm.tm_sec == 30);
	}
}

TEST_CASE("DV packs are found in later DIF sequences")
{
	const auto frame = MakeFrame(kDVFrameSizePAL, 11);
	CHECK(GetDVRecordingTime(frame).has_value());
}

TEST_CASE("Flag bits above the BCD digits are ignored")
{
	auto frame = MakeFrame(kDVFrameSizePAL);
	frame[PackOffset(0, 1, 2) + 2] |= 0xc0; // day: upper two bits are flags
	frame[PackOffset(0, 1, 2) + 3] |= 0xe0; // month: upper three bits are the weekday
	const auto t = GetDVRecordingTime(frame);
	REQUIRE(t.has_value());
	CHECK(ToLocal(*t).tm_mday == 15);
	CHECK(ToLocal(*t).tm_mon == 6);
}

TEST_CASE("Two-digit years pivot at 50")
{
	auto frame = MakeFrame(kDVFrameSizePAL);
	frame[PackOffset(0, 1, 2) + 4] = 0x99;
	const auto t = GetDVRecordingTime(frame);
	REQUIRE(t.has_value());
	CHECK(ToLocal(*t).tm_year == 99);
}

TEST_CASE("Frames without usable timestamps are rejected")
{
	SUBCASE("wrong size")
	{
		auto frame = MakeFrame(kDVFrameSizePAL);
		frame.resize(kDVFrameSizePAL - 1);
		CHECK_FALSE(GetDVRecordingTime(frame).has_value());
		CHECK_FALSE(GetDVRecordingTime({}).has_value());
	}
	SUBCASE("no packs")
	{
		const std::vector<std::uint8_t> frame(kDVFrameSizePAL, 0xff);
		CHECK_FALSE(GetDVRecordingTime(frame).has_value());
	}
	SUBCASE("date pack only")
	{
		auto frame = MakeFrame(kDVFrameSizePAL);
		frame[PackOffset(0, 2, 4)] = 0xff;
		CHECK_FALSE(GetDVRecordingTime(frame).has_value());
	}
	SUBCASE("non-BCD digits")
	{
		auto frame = MakeFrame(kDVFrameSizePAL);
		frame[PackOffset(0, 2, 4) + 3] = 0x4a; // minutes "4A"
		CHECK_FALSE(GetDVRecordingTime(frame).has_value());
	}
	SUBCASE("out-of-range month")
	{
		auto frame = MakeFrame(kDVFrameSizePAL);
		frame[PackOffset(0, 1, 2) + 3] = 0x00;
		CHECK_FALSE(GetDVRecordingTime(frame).has_value());
	}
}
