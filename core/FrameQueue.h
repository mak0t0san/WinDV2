// FrameQueue.h : bounded blocking queue of DV frames between two threads
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace windv {

// Single-producer / single-consumer ring buffer with fixed-size slots. The
// producer (a DirectShow streaming thread) blocks while the queue is full; the
// consumer blocks while it is empty. Close() releases both sides.
class FrameQueue {
public:
	struct Frame {
		std::int64_t duration; // REFERENCE_TIME units
		std::span<const std::uint8_t> data;
	};

	FrameQueue(std::size_t capacity, std::size_t frameSize);

	FrameQueue(const FrameQueue&) = delete;
	FrameQueue& operator=(const FrameQueue&) = delete;

	// Copies a frame in, blocking while the queue is full. Returns false (and
	// drops the frame) once the queue is closed. Throws std::length_error if the
	// frame is larger than the slot size.
	bool Put(std::int64_t duration, std::span<const std::uint8_t> data);

	// Takes the oldest frame, blocking while the queue is empty. Frames still
	// queued at Close() are drained first; after that it returns nullopt.
	// The returned data stays valid until the next call to Get().
	std::optional<Frame> Get();

	void Close();

	bool IsClosed() const { return m_closed.load(); }
	std::size_t Load() const { return m_load.load(); }
	std::size_t Capacity() const { return m_capacity; }
	std::size_t FrameSize() const { return m_frameSize; }

private:
	struct Slot {
		std::int64_t duration = 0;
		std::size_t length = 0;
	};

	std::uint8_t* SlotData(std::size_t index) { return m_storage.data() + index * m_frameSize; }

	const std::size_t m_capacity;
	const std::size_t m_frameSize;
	// One slot more than the capacity, so the slot handed out by the last Get()
	// is never overwritten while the consumer is still reading it.
	const std::size_t m_slotCount;

	std::vector<std::uint8_t> m_storage;
	std::vector<Slot> m_slots;

	std::mutex m_mutex;
	std::condition_variable m_notFull, m_notEmpty;
	std::size_t m_head = 0, m_tail = 0;
	std::atomic<std::size_t> m_load{0};
	std::atomic<bool> m_closed{false};
};

} // namespace windv
