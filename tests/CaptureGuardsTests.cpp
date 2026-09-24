#include <doctest.h>

#include "CaptureGuards.h"

using namespace windv;
using namespace std::chrono_literals;

TEST_CASE("The disk is nearly full below the reserve")
{
	CHECK(IsDiskNearlyFull(0));
	CHECK(IsDiskNearlyFull(kDiskReserveBytes - 1));
	CHECK_FALSE(IsDiskNearlyFull(kDiskReserveBytes));
	CHECK_FALSE(IsDiskNearlyFull(500ull * 1024 * 1024 * 1024));
}

TEST_CASE("Capture directory is the folder part of the base name")
{
	CHECK(CaptureDirectory(L"D:\\dv\\tape") == L"D:\\dv\\");
	CHECK(CaptureDirectory(L"D:\\my.videos\\tape") == L"D:\\my.videos\\");
	CHECK(CaptureDirectory(L"\\\\server\\share\\dv\\tape") == L"\\\\server\\share\\dv\\");
	CHECK(CaptureDirectory(L"D:/dv/tape") == L"D:/dv/");
	CHECK(CaptureDirectory(L"C:tape") == L"C:");
	CHECK(CaptureDirectory(L"tape").empty());
	CHECK(CaptureDirectory(L"").empty());
}

TEST_CASE("DV devices are recognised by their bus")
{
	CHECK(IsDVDevicePath(L"\\\\?\\avc#ven_80046&mod_0&camcorder&dv#1d4b860101460008#{65e8773d-8f56-11d0-a3b9-"
	                     L"00a0c9223196}\\global"));
	CHECK(IsDVDevicePath(L"\\\\?\\AVC#VEN_80046&MOD_0&CAMCORDER&DV#1D4B860101460008#{guid}"));
	CHECK(IsDVDevicePath(L"##?#AVC#VEN_80046&MOD_0&CAMCORDER&DV#1D4B860101460008#{guid}"));
	CHECK(IsDVDevicePath(L"\\\\?\\61883#something#{guid}"));

	CHECK_FALSE(IsDVDevicePath(L"\\\\?\\usb#vid_046d&pid_0825&mi_00#6&1234#{guid}\\global"));
	CHECK_FALSE(IsDVDevicePath(L"\\\\?\\root#media#0000#{guid}"));
	CHECK_FALSE(IsDVDevicePath(L"\\\\?\\bthenum#{0000110a}#{guid}"));
	CHECK_FALSE(IsDVDevicePath(L""));
	CHECK_FALSE(IsDVDevicePath(L"avc")); // too short to be a bus prefix
}

TEST_CASE("Signal watch fires only after frames stop for the timeout")
{
	const SignalWatch::Clock::time_point t0{};
	SignalWatch watch(5s);

	CHECK_FALSE(watch.Lost(t0 + 60s)); // no frames yet: the tape may not be rolling

	watch.Frame(t0);
	CHECK_FALSE(watch.Lost(t0 + 4999ms));
	CHECK(watch.Lost(t0 + 5s));

	watch.Frame(t0 + 10s); // the signal came back
	CHECK_FALSE(watch.Lost(t0 + 12s));

	watch.Reset();
	CHECK_FALSE(watch.Lost(t0 + 60s));
}

TEST_CASE("A zero timeout turns the signal watch off")
{
	const SignalWatch::Clock::time_point t0{};
	SignalWatch watch;
	watch.Frame(t0);
	CHECK_FALSE(watch.Lost(t0 + 1h));
}
