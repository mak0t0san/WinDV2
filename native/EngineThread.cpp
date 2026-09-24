// EngineThread.cpp : a COM MTA thread that runs the engine's work in order

#include "DShowBase.h"
#include "EngineThread.h"

#include "ComApartment.h"

namespace {

constexpr auto kTickInterval = std::chrono::milliseconds(200);

// Waits for event, dispatching messages sent to this thread from other threads
// (but not posted ones, so no UI code runs re-entrantly).
void WaitPumpingSentMessages(HANDLE event)
{
	for (;;) {
		const DWORD result = MsgWaitForMultipleObjectsEx(1, &event, INFINITE, QS_SENDMESSAGE, 0);
		if (result != WAIT_OBJECT_0 + 1)
			return; // signalled (or failed: don't spin)
		MSG msg;
		PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
	}
}

} // namespace

EngineThread::EngineThread(std::function<void()> tick) : m_tick(std::move(tick))
{
	m_thread = std::jthread([this](std::stop_token stop) { Run(stop); });
}

EngineThread::~EngineThread()
{
	m_thread.request_stop();
	m_wake.notify_all();
	if (m_thread.joinable())
		m_thread.join();
}

void EngineThread::Post(std::function<void()> task)
{
	{
		std::lock_guard lock(m_mutex);
		m_tasks.push_back(std::move(task));
	}
	m_wake.notify_all();
}

void EngineThread::Invoke(const std::function<void()>& task)
{
	if (IsCurrent()) {
		task();
		return;
	}

	const HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!done) {
		// Without an event the only safe thing is to run it here and now.
		task();
		return;
	}
	Post([&task, done] {
		task();
		SetEvent(done);
	});
	if (IsGUIThread(FALSE))
		WaitPumpingSentMessages(done);
	else
		WaitForSingleObject(done, INFINITE);
	CloseHandle(done);
}

void EngineThread::Run(std::stop_token stop)
{
	ComApartment com;
	for (;;) {
		std::function<void()> task;
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait_for(lock, stop, kTickInterval, [this] { return !m_tasks.empty(); });
			if (!m_tasks.empty()) {
				task = std::move(m_tasks.front());
				m_tasks.pop_front();
			} else if (stop.stop_requested()) {
				return; // all queued work is done
			}
		}
		if (task)
			task();
		if (m_tick)
			m_tick();
	}
}
