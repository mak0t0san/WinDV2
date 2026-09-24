#include <doctest.h>

#include "FrameQueue.h"

#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace windv;
using namespace std::chrono_literals;

namespace {

std::vector<std::uint8_t> Bytes(std::size_t size, std::uint8_t value)
{
	return std::vector<std::uint8_t>(size, value);
}

} // namespace

TEST_CASE("Frames come out in order with their durations")
{
	FrameQueue queue(4, 16);
	REQUIRE(queue.Put(100, Bytes(16, 1)));
	REQUIRE(queue.Put(200, Bytes(8, 2)));
	CHECK(queue.Load() == 2);

	auto first = queue.Get();
	REQUIRE(first.has_value());
	CHECK(first->duration == 100);
	CHECK(first->data.size() == 16);
	CHECK(first->data[15] == 1);

	auto second = queue.Get();
	REQUIRE(second.has_value());
	CHECK(second->duration == 200);
	CHECK(second->data.size() == 8);
	CHECK(second->data[0] == 2);
	CHECK(queue.Load() == 0);
}

TEST_CASE("Oversized frames are rejected")
{
	FrameQueue queue(2, 16);
	CHECK_THROWS_AS(queue.Put(0, Bytes(17, 0)), std::length_error);
	CHECK(queue.Load() == 0);
}

TEST_CASE("The last frame handed out survives a full queue")
{
	FrameQueue queue(3, 4);
	REQUIRE(queue.Put(0, Bytes(4, 9)));
	auto held = queue.Get();
	REQUIRE(held.has_value());
	for (std::uint8_t i = 0; i < 3; ++i)
		REQUIRE(queue.Put(0, Bytes(4, i)));
	CHECK(queue.Load() == 3);
	CHECK(held->data[0] == 9);
}

TEST_CASE("Put blocks while full until the consumer takes a frame")
{
	FrameQueue queue(1, 4);
	REQUIRE(queue.Put(1, Bytes(4, 1)));

	auto producer = std::async(std::launch::async, [&] { return queue.Put(2, Bytes(4, 2)); });
	CHECK(producer.wait_for(50ms) == std::future_status::timeout);

	REQUIRE(queue.Get().has_value());
	REQUIRE(producer.wait_for(5s) == std::future_status::ready);
	CHECK(producer.get());
	CHECK(queue.Get()->duration == 2);
}

TEST_CASE("Get blocks while empty until a frame arrives")
{
	FrameQueue queue(2, 4);
	auto consumer = std::async(std::launch::async, [&] { return queue.Get().has_value(); });
	CHECK(consumer.wait_for(50ms) == std::future_status::timeout);

	REQUIRE(queue.Put(0, Bytes(4, 0)));
	REQUIRE(consumer.wait_for(5s) == std::future_status::ready);
	CHECK(consumer.get());
}

TEST_CASE("Close wakes blocked threads and drains remaining frames")
{
	SUBCASE("blocked consumer")
	{
		FrameQueue queue(2, 4);
		auto consumer = std::async(std::launch::async, [&] { return queue.Get().has_value(); });
		std::this_thread::sleep_for(20ms);
		queue.Close();
		REQUIRE(consumer.wait_for(5s) == std::future_status::ready);
		CHECK_FALSE(consumer.get());
	}
	SUBCASE("blocked producer")
	{
		FrameQueue queue(1, 4);
		REQUIRE(queue.Put(0, Bytes(4, 0)));
		auto producer = std::async(std::launch::async, [&] { return queue.Put(0, Bytes(4, 0)); });
		std::this_thread::sleep_for(20ms);
		queue.Close();
		REQUIRE(producer.wait_for(5s) == std::future_status::ready);
		CHECK_FALSE(producer.get());
	}
	SUBCASE("queued frames are still delivered")
	{
		FrameQueue queue(4, 4);
		REQUIRE(queue.Put(7, Bytes(4, 0)));
		queue.Close();
		CHECK(queue.IsClosed());
		CHECK_FALSE(queue.Put(8, Bytes(4, 0)));
		auto frame = queue.Get();
		REQUIRE(frame.has_value());
		CHECK(frame->duration == 7);
		CHECK_FALSE(queue.Get().has_value());
	}
}

TEST_CASE("Frames survive a producer/consumer stress run intact")
{
	constexpr int kFrames = 5000;
	FrameQueue queue(8, 64);

	std::thread producer([&] {
		for (int i = 0; i < kFrames; ++i)
			queue.Put(i, Bytes(1 + i % 64, static_cast<std::uint8_t>(i)));
		queue.Close();
	});

	int received = 0;
	bool intact = true;
	while (auto frame = queue.Get()) {
		const int i = static_cast<int>(frame->duration);
		intact = intact && i == received && frame->data.size() == static_cast<std::size_t>(1 + i % 64) &&
		         frame->data.back() == static_cast<std::uint8_t>(i);
		++received;
	}
	producer.join();
	CHECK(intact);
	CHECK(received == kFrames);
}
