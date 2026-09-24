// FrameQueue.cpp : bounded blocking queue of DV frames between two threads

#include "FrameQueue.h"

#include <algorithm>
#include <stdexcept>

namespace windv {

FrameQueue::FrameQueue(std::size_t capacity, std::size_t frameSize)
    : m_capacity(std::max<std::size_t>(capacity, 1)),
      m_frameSize(frameSize),
      m_slotCount(m_capacity + 1),
      m_storage(m_slotCount * frameSize),
      m_slots(m_slotCount)
{}

bool FrameQueue::Put(std::int64_t duration, std::span<const std::uint8_t> data)
{
	if (data.size() > m_frameSize)
		throw std::length_error("DV frame is larger than the queue slot");

	std::unique_lock lock(m_mutex);
	m_notFull.wait(lock, [this] { return m_closed.load() || m_load.load() < m_capacity; });
	if (m_closed.load())
		return false;

	m_slots[m_tail] = {duration, data.size()};
	std::copy(data.begin(), data.end(), SlotData(m_tail));
	m_tail = (m_tail + 1) % m_slotCount;
	++m_load;
	lock.unlock();
	m_notEmpty.notify_one();
	return true;
}

std::optional<FrameQueue::Frame> FrameQueue::Get()
{
	std::unique_lock lock(m_mutex);
	m_notEmpty.wait(lock, [this] { return m_closed.load() || m_load.load() > 0; });
	if (m_load.load() == 0)
		return std::nullopt;

	const Slot& slot = m_slots[m_head];
	Frame frame{slot.duration, {SlotData(m_head), slot.length}};
	m_head = (m_head + 1) % m_slotCount;
	--m_load;
	lock.unlock();
	m_notFull.notify_one();
	return frame;
}

void FrameQueue::Close()
{
	{
		std::lock_guard lock(m_mutex);
		m_closed = true;
	}
	m_notFull.notify_all();
	m_notEmpty.notify_all();
}

} // namespace windv
