#include <doctest.h>

#include "CaptureNaming.h"

#include <vector>

using namespace windv;

namespace {

std::wstring Next(std::wstring_view stem, int ndigits, std::vector<std::wstring> existing = {})
{
	return NextCaptureFilename(stem, ndigits, existing);
}

} // namespace

TEST_CASE("Capture stem joins base and date")
{
	CHECK(CaptureStem(L"C:\\dv\\tape", L"04-07-15_13-45") == L"C:\\dv\\tape.04-07-15_13-45");
	CHECK(CaptureStem(L"C:\\dv\\tape", L"") == L"C:\\dv\\tape");
	CHECK(CaptureSearchPattern(L"C:\\dv\\tape") == L"C:\\dv\\tape*.avi");
}

TEST_CASE("Capture base strips the date, number and extension")
{
	CHECK(CaptureBaseFromFilename(L"D:\\dv\\tape.04-07-15.00.avi") == L"D:\\dv\\tape");
	CHECK(CaptureBaseFromFilename(L"D:\\my.videos\\tape") == L"D:\\my.videos\\tape");
	CHECK(CaptureBaseFromFilename(L"D:/my.videos/tape.avi") == L"D:/my.videos/tape");
	CHECK(CaptureBaseFromFilename(L"C:tape.avi") == L"C:tape");
	CHECK(CaptureBaseFromFilename(L"tape") == L"tape");
	CHECK(CaptureBaseFromFilename(L".avi").empty());
	CHECK(CaptureBaseFromFilename(L"").empty());
}

TEST_CASE("Numbered captures start at zero with the requested width")
{
	CHECK(Next(L"C:\\dv\\tape", 2) == L"C:\\dv\\tape.00.avi");
	CHECK(Next(L"C:\\dv\\tape", 1) == L"C:\\dv\\tape.0.avi");
	CHECK(Next(L"tape", 4) == L"tape.0000.avi");
}

TEST_CASE("Numbering continues after the highest existing number")
{
	CHECK(Next(L"C:\\dv\\tape", 2, {L"tape.00.avi", L"tape.01.avi", L"tape.05.avi"}) == L"C:\\dv\\tape.06.avi");
	// Matching is case-insensitive like the file system.
	CHECK(Next(L"C:\\dv\\tape", 2, {L"TAPE.03.AVI"}) == L"C:\\dv\\tape.04.avi");
}

TEST_CASE("Width grows past the requested digits and never shrinks")
{
	CHECK(Next(L"tape", 2, {L"tape.99.avi"}) == L"tape.100.avi");
	CHECK(Next(L"tape", 2, {L"tape.100.avi", L"tape.99.avi"}) == L"tape.101.avi");
	CHECK(Next(L"tape", 2, {L"tape.99.avi", L"tape.100.avi"}) == L"tape.101.avi");
	// Numbers narrower than the requested width are ignored.
	CHECK(Next(L"tape", 3, {L"tape.7.avi"}) == L"tape.000.avi");
}

TEST_CASE("With zero digits the first capture is unnumbered")
{
	CHECK(Next(L"tape", 0) == L"tape.avi");
	CHECK(Next(L"tape", 0, {L"tape.avi"}) == L"tape.0.avi");
	CHECK(Next(L"tape", 0, {L"tape.avi", L"tape.0.avi"}) == L"tape.1.avi");
}

TEST_CASE("Unrelated files are ignored")
{
	const std::vector<std::wstring> existing = {
	    L"tape2.05.avi",        L"tape.xx.avi", L"tape.05.avi.bak", L"tape..avi", L"tape.5a.avi", L"other.07.avi",
	    L"tape.1234567890.avi", // too long to be one of ours
	};
	CHECK(Next(L"tape", 2, existing) == L"tape.00.avi");
}

TEST_CASE("The stem's directory does not affect matching")
{
	CHECK(Next(L"D:/video/tape", 2, {L"tape.00.avi"}) == L"D:/video/tape.01.avi");
	CHECK(Next(L"D:tape", 2, {L"tape.00.avi"}) == L"D:tape.01.avi");
}
