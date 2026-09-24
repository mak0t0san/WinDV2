// EngineThread.h : a COM MTA thread that runs the engine's work in order
#pragma once

#include "DShowBase.h"

#include <deque>
#include <functional>

// Runs tasks one at a time on its own thread in the multithreaded apartment, and
// calls tick() between them (at least every 200 ms) for periodic work.
//
// The thread runs a message loop while it waits. DirectShow creates the video
// renderer's window on the thread that builds the graph, and that window is a
// child of the UI's window: if this thread didn't dispatch messages, every
// click or move in the UI would wait on it forever.
class EngineThread {
public:
	explicit EngineThread(std::function<void()> tick);
	~EngineThread();

	EngineThread(const EngineThread&) = delete;
	EngineThread& operator=(const EngineThread&) = delete;

	// Queues a task and returns at once.
	void Post(std::function<void()> task);

	// Runs a task and waits for it. A caller with a message queue (a UI thread)
	// keeps handling sent messages while it waits: the engine's video renderer
	// is a child of the caller's window and sends it messages synchronously.
	// task must not throw.
	void Invoke(const std::function<void()>& task);

	bool IsCurrent() const { return std::this_thread::get_id() == m_thread.get_id(); }

private:
	void Run(std::stop_token stop);
	std::function<void()> TakeTask();

	std::function<void()> m_tick;
	std::mutex m_mutex;
	std::deque<std::function<void()>> m_tasks;
	HANDLE m_wake; // auto-reset: tasks queued or stop requested
	std::jthread m_thread;
};
