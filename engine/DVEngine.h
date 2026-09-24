// DVEngine.h : owns the DV pipeline, its state machine and worker thread
//
// Capture: CDVInput -> FrameQueue -> worker -> CAVIWriter (+ CMonitor preview)
// Record:  CAVIJoiner -> FrameQueue -> worker -> CDVOutput (+ CMonitor preview)
//
// Public methods are called from one controlling thread, which must be in the
// COM multithreaded apartment. Events arrive on worker threads.
#pragma once

#include "DShowBase.h"
#include "FrameInterfaces.h"
#include "FrameQueue.h"
#include "TransportLogic.h"

class CAVIJoiner;
class CAVIWriter;
class CDVInput;
class CDVOutput;
class CMonitor;

// Notifications from the engine. Called on worker threads: implementations
// must only post to their own thread, never call back into the engine.
class DVEngineEvents {
public:
	// The DV recording timestamp changed; read it with DVEngine::GetDVTime().
	virtual void OnDVTimeChanged() = 0;
	// A worker failed; fetch the message with DVEngine::TakeError().
	virtual void OnError() = 0;

protected:
	~DVEngineEvents() = default;
};

class DVEngine : private CFrameHandler {
public:
	enum State { Idle, RecordPaused, Recording, CapturePaused, Capturing, Finished };

	explicit DVEngine(DVEngineEvents* events);
	~DVEngine() override;

	DVEngine(const DVEngine&) = delete;
	DVEngine& operator=(const DVEngine&) = delete;

	// Settings; written by the controlling thread, read by the worker threads.
	std::atomic<bool> m_type2AVI{true};
	std::atomic<int> m_discontinuityThreshold{1};
	std::atomic<int> m_maxAVIFrames{25 * 60 * 15};
	std::atomic<int> m_everyNth{1};
	std::atomic<bool> m_recordPreview{true};
	std::atomic<bool> m_DVctrl{false};

	// The window the preview is drawn into; takes effect at the next Build*().
	void SetPreviewWindow(HWND hWnd) { m_previewWnd = hWnd; }
	// Call when the preview window has been resized.
	void ResizePreview();

	State GetState() const { return m_state; }
	long GetDropped() const { return m_dropped; }
	std::size_t GetQueueLoad() const;
	long GetCounter() const { return m_counter; }
	REFERENCE_TIME GetTime() const { return m_time; }
	std::time_t GetDVTime() const { return m_dvTime; }
	// Frames delivered by the source since the pipeline was built; shows whether
	// a signal is arriving at all.
	long GetFramesReceived() const
	{
		return m_framesReceived;
	} // Returns and clears the last error reported by a worker thread.
	std::wstring TakeError();

	void Destroy();

	void BuildCapturing(const std::wstring& device);
	void StartCapturing(const std::wstring& filename, const std::wstring& dtformat, int ndigits,
	                    REFERENCE_TIME captureTime = 0);
	void StopCapturing();
	void BuildRecording(const std::wstring& filenames, const std::wstring& device);
	void StartRecording();
	void StopRecording();

	// Deck transport. Buttons act on the capture source only; while recording to
	// tape the deck follows Start/StopRecording. Independent of m_DVctrl.
	bool CanControlDeck() const;
	windv::DeckMode GetDeckMode();
	void Transport(windv::DeckCommand command);

private:
	struct CaptureTarget {
		std::wstring filename, dtformat;
		int ndigits = 0;
	};

	// CFrameHandler: frames from the source, on its streaming thread.
	void HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame) override;
	void EndOfStream() override;
	void SourceError(const std::wstring& message) override;

	void CapturingThread(std::stop_token stop);
	void RecordingThread(std::stop_token stop);
	void CaptureLoop();
	void RecordLoop();
	void FinishWriter();
	void ReportError(const std::wstring& message);
	void NotifyTimeChange(std::time_t dvTime);
	void StartWorker(void (DVEngine::*worker)(std::stop_token));
	void CreateMonitor(const CMediaType& type);

	DVEngineEvents* const m_events;
	HWND m_previewWnd = nullptr;
	std::atomic<State> m_state{Idle};

	std::unique_ptr<CAVIJoiner> m_aviJoiner;
	std::unique_ptr<CAVIWriter> m_aviWriter; // only touched by the capture thread
	std::unique_ptr<CDVInput> m_dvInput;
	std::unique_ptr<CDVOutput> m_dvOutput;
	std::unique_ptr<CMonitor> m_monitor;
	std::unique_ptr<windv::FrameQueue> m_queue;
	std::jthread m_thread;

	std::mutex m_mutex; // guards m_target and m_error
	CaptureTarget m_target;
	std::wstring m_error;

	std::atomic<long> m_dropped{0};
	std::atomic<long> m_counter{-1};
	std::atomic<REFERENCE_TIME> m_time{-1};
	std::atomic<REFERENCE_TIME> m_captureTime{0};
	std::atomic<std::time_t> m_dvTime{0};
	std::atomic<long> m_framesReceived{0};
};
