#include <doctest.h>

#include "CommandLine.h"

#include <vector>

using namespace windv;

namespace {

constexpr std::int64_t kSecond = 10'000'000;

std::optional<CommandLine> Parse(std::vector<std::wstring> args)
{
	return ParseCommandLine(args);
}

} // namespace

TEST_CASE("Durations accept seconds, minutes and hours")
{
	CHECK(ParseDuration(L"0") == 0);
	CHECK(ParseDuration(L"90") == 90 * kSecond);
	CHECK(ParseDuration(L"1:30") == 90 * kSecond);
	CHECK(ParseDuration(L"1:02:03") == (3600 + 120 + 3) * kSecond);
	CHECK(ParseDuration(L"00:00:05") == 5 * kSecond);
}

TEST_CASE("Duration fractions are in fractions of a second")
{
	CHECK(ParseDuration(L"1.5") == kSecond + kSecond / 2);
	CHECK(ParseDuration(L"0:01.25") == kSecond + kSecond / 4);
	CHECK(ParseDuration(L"2.") == 2 * kSecond);
	CHECK(ParseDuration(L"0.0000001") == 1);
	CHECK(ParseDuration(L"0.123456789") == 1234567); // beyond 100 ns is dropped
}

TEST_CASE("Malformed durations are rejected")
{
	for (const wchar_t* text : {L"", L"abc", L"1:", L":30", L"1::2", L"1:2:3:4", L"1.2.3", L"1.x", L"-5", L"1 ",
	                            L"1234567890"}) {
		CAPTURE(std::wstring(text));
		CHECK_FALSE(ParseDuration(text).has_value());
	}
}

TEST_CASE("No arguments means interactive mode")
{
	const auto cl = Parse({});
	REQUIRE(cl.has_value());
	CHECK(cl->mode == CommandLine::Mode::Interactive);
}

TEST_CASE("capture takes a duration and one file")
{
	const auto cl = Parse({L"capture", L"0:10", L"C:\\My Videos\\tape"});
	REQUIRE(cl.has_value());
	CHECK(cl->mode == CommandLine::Mode::Capture);
	CHECK_FALSE(cl->exitOnFinish);
	CHECK(cl->duration == 10 * kSecond);
	CHECK(cl->captureFile == L"C:\\My Videos\\tape");

	const auto withExit = Parse({L"capture", L"-exit", L"0", L"tape"});
	REQUIRE(withExit.has_value());
	CHECK(withExit->exitOnFinish);
	CHECK(withExit->duration == 0);
}

TEST_CASE("record takes one or more files")
{
	const auto cl = Parse({L"record", L"-exit", L"a.avi", L"C:\\path with spaces\\b.avi"});
	REQUIRE(cl.has_value());
	CHECK(cl->mode == CommandLine::Mode::Record);
	CHECK(cl->exitOnFinish);
	REQUIRE(cl->recordFiles.size() == 2);
	CHECK(cl->recordFiles[1] == L"C:\\path with spaces\\b.avi");
}

TEST_CASE("Usage errors")
{
	CHECK_FALSE(Parse({L"capture"}).has_value());
	CHECK_FALSE(Parse({L"capture", L"-exit"}).has_value());
	CHECK_FALSE(Parse({L"capture", L"10"}).has_value());
	CHECK_FALSE(Parse({L"capture", L"10", L"a", L"b"}).has_value());
	CHECK_FALSE(Parse({L"capture", L"ten", L"a"}).has_value());
	CHECK_FALSE(Parse({L"record"}).has_value());
	CHECK_FALSE(Parse({L"record", L"-exit"}).has_value());
	CHECK_FALSE(Parse({L"play", L"a.avi"}).has_value());
}
