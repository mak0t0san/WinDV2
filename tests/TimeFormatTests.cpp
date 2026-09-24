#include <doctest.h>

#include "TimeFormat.h"

using namespace windv;

namespace {

std::time_t MakeLocal(int year, int month, int day, int hour, int min, int sec)
{
	std::tm tm{};
	tm.tm_year = year - 1900;
	tm.tm_mon = month - 1;
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec;
	tm.tm_isdst = -1;
	return std::mktime(&tm);
}

} // namespace

TEST_CASE("Default WinDV formats are valid and format as expected")
{
	const std::time_t t = MakeLocal(2004, 7, 15, 13, 45, 30);
	CHECK(FormatTime(L"%y-%m-%d_%H-%M", t) == L"04-07-15_13-45");
	CHECK(FormatTime(L"%Y-%m-%d_%H-%M-%S", t) == L"2004-07-15_13-45-30");
	CHECK(FormatTime(L"%Y%m%d-%H%M%S", t) == L"20040715-134530");
	CHECK(FormatTime(L"%#d", t) == L"15");
	CHECK(FormatTime(L"100%%", t) == L"100%");
	CHECK(FormatTime(L"~%y", t) == L"~04");
}

TEST_CASE("Empty format gives an empty string")
{
	CHECK(FormatTime(L"", 0).empty());
	CHECK(IsValidTimeFormat(L""));
}

TEST_CASE("Invalid formats are rejected instead of reaching wcsftime")
{
	// Each of these makes the UCRT invoke the invalid parameter handler.
	for (const wchar_t* format : {L"%", L"abc%", L"%Q", L"%y-%", L"%E", L"%Ed", L"%O", L"%OY", L"%#", L"%#Q", L"%k"}) {
		CAPTURE(std::wstring(format));
		CHECK_FALSE(IsValidTimeFormat(format));
		CHECK(FormatTime(format, 0).empty());
	}
}

TEST_CASE("Modified conversions are accepted where the UCRT allows them")
{
	CHECK(IsValidTimeFormat(L"%Ey %EY %Ec %Od %OH %Oy"));
	CHECK(IsValidTimeFormat(L"%#c %#x %#H"));
}

TEST_CASE("Long output is not truncated")
{
	std::wstring format;
	for (int i = 0; i < 200; ++i)
		format += L"%Y";
	CHECK(FormatTime(format, MakeLocal(2004, 7, 15, 0, 0, 0)).size() == 800);
}
