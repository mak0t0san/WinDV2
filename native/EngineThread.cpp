// EngineThread.cpp : a COM MTA thread that runs the engine's work in order

#include "DShowBase.h"
#include "EngineThread.h"

#include "ComApartment.h"
#include "DShowError.h"

namespace {

constexpr ULONGLONG kTickIntervalMs = 200;

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

void DispatchPendingMessages()
{
	MSG msg;
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

} // namespace

EngineThread::EngineThread(std::function<void()> tick)
    : m_tick(std::move(tick)), m_wake(CreateEventW(nullptr, FALSE, FALSE, nullptr))
{
	if (!m_wake)
		throw DShowError(L"Can't create the engine thread's event", HRESULT_FROM_WIN32(GetLastError()));
	m_thread = std::jthread([this](std::stop_token stop) { Run(stop); });
}

EngineThread::~EngineThread()
{
	m_thread.request_stop();
	SetEvent(m_wake);
	if (m_thread.joinable())
		m_thread.join();
	CloseHandle(m_wake);
}

void EngineThread::Post(std::function<void()> task)
{
	{
		std::lock_guard lock(m_mutex);
		m_tasks.push_back(std::move(task));
	}
	SetEvent(m_wake);
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

std::function<void()> EngineThread::TakeTask()
{
	std::lock_guard lock(m_mutex);
	if (m_tasks.empty())
		return {};
	std::function<void()> task = std::move(m_tasks.front());
	m_tasks.pop_front();
	return task;
}

void EngineThread::Run(std::stop_token stop)
{
	ComApartment com;
	// Make this a GUI thread with a message queue before any window exists on it.
	MSG msg;
	PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);

	ULONGLONG nextTick = GetTickCount64();
	for (;;) {
		// Everything queued, then windows' messages, then periodic work.
		while (std::function<void()> task = TakeTask()) {
			task();
			DispatchPendingMessages();
		}
		if (stop.stop_requested())
			return; // all queued work is done

		DispatchPendingMessages();
		const ULONGLONG now = GetTickCount64();
		if (now >= nextTick) {
			if (m_tick)
				m_tick();
			nextTick = now + kTickIntervalMs;
		}

		const DWORD timeout = static_cast<DWORD>(nextTick > now ? nextTick - now : 0);
		MsgWaitForMultipleObjectsEx(1, &m_wake, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
	}
}
