// DShow.h : DirectShow pipeline - DV frame sources, sinks and the CDV controller
//
// A CFrameSource pushes raw DV frames into a CFrameHandler. Sources and sinks are
// small DirectShow graphs built around custom filters (CInputGraph wraps an input
// pin, COutputGraph an output pin), so the application sees every frame itself.
// CDV owns the pipeline and moves frames between threads through a FrameQueue.
#pragma once

#include "FrameQueue.h"

// Posted to the CDV's parent when the DV recording timestamp changes; read the
// new value with CDV::GetDVTime().
constexpr UINT WM_DV_TIMECHANGE = WM_USER + 201;
// Posted to the CDV's parent when a worker thread fails; fetch the message with
// CDV::TakeError().
constexpr UINT WM_DV_ERROR = WM_USER + 202;

/////////////////////////////////////////////////////////////////////////////
// Errors

class DShowError : public std::runtime_error {
public:
	enum class Cause { Error, DeviceNotFound };

	explicit DShowError(const CString& message, HRESULT hr = S_OK, Cause cause = Cause::Error);

	// The user-facing message, including the HRESULT and its description.
	const CString& Message() const { return m_message; }
	HRESULT Result() const { return m_hr; }
	Cause GetCause() const { return m_cause; }

private:
	CString m_message;
	HRESULT m_hr;
	Cause m_cause;
};

// Throws DShowError unless hr is exactly S_OK.
void CheckHR(HRESULT hr, LPCWSTR what);
// Throws DShowError if hr is a failure code; success codes such as S_FALSE pass.
void CheckSucceeded(HRESULT hr, LPCWSTR what);

/////////////////////////////////////////////////////////////////////////////
// Frame source / handler interfaces

class CFrameHandler {
public:
	virtual ~CFrameHandler() = default;
	virtual void HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame) = 0;
	// The source has no more frames.
	virtual void EndOfStream() {}
	// The source failed on one of its own threads and has stopped.
	virtual void SourceError(const CString& /*message*/) {}
};

class CFrameSource {
public:
	virtual ~CFrameSource() = default;
	virtual void GetMediaType(CMediaType* type) = 0;
	virtual void Run(CFrameHandler* handler) = 0;
	virtual void Stop() = 0;
};

/////////////////////////////////////////////////////////////////////////////
// Graph wrappers

class CFilterGraph {
public:
	CFilterGraph();
	virtual ~CFilterGraph();

	CFilterGraph(const CFilterGraph&) = delete;
	CFilterGraph& operator=(const CFilterGraph&) = delete;

protected:
	CComPtr<ICaptureGraphBuilder2> m_GB;
	CComPtr<IGraphBuilder> m_FG;
	CComPtr<IMediaControl> m_MC;
	CComPtr<IMediaSeeking> m_MS;
	CComPtr<IMediaEventEx> m_ME;
};

// A graph that ends in our own input pin, which hands each sample to a handler.
class CInputGraph : public CFilterGraph, public CFrameSource {
public:
	CInputGraph();
	~CInputGraph() override;

	void GetMediaType(CMediaType* type) override;
	void Run(CFrameHandler* handler) override;
	void Stop() override;

protected:
	class CInputFilter;
	// COM reference keeping m_inputFilter alive; CBaseFilter derives from IUnknown
	// twice, so it cannot sit in a CComPtr of its own type.
	CComPtr<IBaseFilter> m_inputFilterRef;
	CInputFilter* m_inputFilter = nullptr;
	IPin* InputPin() const;
	bool IsInputConnected() const;

private:
	std::atomic<CFrameHandler*> m_handler{nullptr};
};

// A graph that starts at our own output pin, fed by HandleFrame().
class COutputGraph : public CFilterGraph, public CFrameHandler {
public:
	// queueDepth > 0 delivers through a COutputQueue with that many buffers.
	COutputGraph(const CMediaType& type, int queueDepth = 0);
	~COutputGraph() override;

	void HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame) override;

protected:
	class COutputFilter;
	CComPtr<IBaseFilter> m_outputFilterRef;
	COutputFilter* m_outputFilter = nullptr;

	HRESULT GetDeliveryBuffer(IMediaSample** sample);
	HRESULT Deliver(IMediaSample* sample);
	void DeliverEndOfStream();
	void WaitForCompletion();

private:
	CMediaType m_type;
	REFERENCE_TIME m_time = 0;
	int m_queueDepth;
};

/////////////////////////////////////////////////////////////////////////////
// Sources

class CAVIReader : public CInputGraph {
public:
	explicit CAVIReader(const CString& filename);
};

// Plays a list of AVI files back to back as one stream. Each file needs its own
// graph, so the next one is built on a helper thread when the current one ends.
class CAVIJoiner : public CFrameSource, public CFrameHandler {
public:
	// filenames: '|'-separated list; each entry may contain wildcards.
	explicit CAVIJoiner(const CString& filenames);
	~CAVIJoiner() override;

	void GetMediaType(CMediaType* type) override;
	void Run(CFrameHandler* handler) override;
	void Stop() override;

	void HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame) override;
	void EndOfStream() override;

private:
	void JoinerThread(std::stop_token stop);

	std::vector<CString> m_filenames;
	std::size_t m_next = 0;
	std::unique_ptr<CAVIReader> m_reader;
	std::atomic<CFrameHandler*> m_handler{nullptr};
	std::atomic<bool> m_stopping{false};

	std::mutex m_mutex;
	std::condition_variable_any m_readerEnded;
	bool m_readerEndedFlag = false;
	std::jthread m_thread;
};

// Camcorder transport control (play/pause/record) through IAMExtTransport.
class CDVControl {
public:
	void CtrlAttach(IUnknown* device);
	void CtrlStop();
	void CtrlPlay();
	void CtrlPause();
	void CtrlRecord();
	void CtrlRecPause();

private:
	CComPtr<IAMExtTransport> m_ET;
};

class CDVInput : public CInputGraph, public CDVControl {
public:
	explicit CDVInput(const CString& device);
	long GetDroppedFrames();

private:
	CComPtr<IAMDroppedFrames> m_DF;
};

/////////////////////////////////////////////////////////////////////////////
// Sinks

class CDVOutput : public COutputGraph, public CDVControl {
public:
	CDVOutput(const CString& device, const CMediaType& type);
	~CDVOutput() override;
};

// Writes one AVI file. The file is written under a temporary "~" name and moved
// to its final name in Finish(), once the DV timestamp for the name is known.
class CAVIWriter : public COutputGraph {
public:
	CAVIWriter(const CString& base, const CString& dtformat, int ndigits, std::time_t dvTime, bool type2AVI,
	           const CMediaType& type);
	~CAVIWriter() override;

	// Flushes the file and renames it. Throws DShowError if the rename fails.
	void Finish();

	std::time_t m_dvTime; // used for the final name; 0 means "now"

private:
	CString m_tmpfile, m_base, m_dtformat;
	int m_ndigits;
	bool m_finished = false;
};

// On-screen preview. Frames are offered with HandleFrame(); a helper thread
// takes one whenever the renderer is ready, so preview never slows capture.
class CMonitor : public COutputGraph {
public:
	CMonitor(HWND hWnd, const CMediaType& type);
	~CMonitor() override;

	void Resize();
	void HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame) override;

private:
	void MonitoringThread(std::stop_token stop);

	HWND m_hWnd;
	CComPtr<IVideoWindow> m_VW;

	std::mutex m_mutex;
	std::condition_variable_any m_filled;
	CComPtr<IMediaSample> m_sample; // empty buffer waiting for a frame
	bool m_sampleFilled = false;
	std::jthread m_thread;
};

/////////////////////////////////////////////////////////////////////////////
// CDV - owns the pipeline and the state machine; also the preview window

class CDV : public CStatic, public CFrameHandler {
public:
	enum State { Idle, RecordPaused, Recording, CapturePaused, Capturing, Finished };

	CDV();
	~CDV() override;

	// Settings; written by the UI thread, read by the worker threads.
	std::atomic<bool> m_type2AVI{true};
	std::atomic<int> m_discontinuityThreshold{1};
	std::atomic<int> m_maxAVIFrames{25 * 60 * 15};
	std::atomic<int> m_everyNth{1};
	std::atomic<bool> m_recordPreview{true};
	std::atomic<bool> m_DVctrl{false};

	State GetState() const { return m_state; }
	long GetDropped() const { return m_dropped; }
	std::size_t GetQueueLoad() const;
	long GetCounter() const { return m_counter; }
	REFERENCE_TIME GetTime() const { return m_time; }
	std::time_t GetDVTime() const { return m_dvTime; }
	// Returns and clears the last error reported by a worker thread.
	CString TakeError();

	void Destroy();

	void BuildCapturing(const CString& device);
	void StartCapturing(const CString& filename, const CString& dtformat, int ndigits, REFERENCE_TIME captureTime = 0);
	void StopCapturing();
	void BuildRecording(const CString& filenames, const CString& device);
	void StartRecording();
	void StopRecording();

protected:
	void HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame) override;
	void EndOfStream() override;
	void SourceError(const CString& message) override;

	afx_msg void OnSize(UINT nType, int cx, int cy);
	DECLARE_MESSAGE_MAP()

private:
	struct CaptureTarget {
		CString filename, dtformat;
		int ndigits = 0;
	};

	void CapturingThread(std::stop_token stop);
	void RecordingThread(std::stop_token stop);
	void CaptureLoop();
	void RecordLoop();
	void FinishWriter();
	void ReportError(const CString& message);
	void NotifyTimeChange(std::time_t dvTime);
	void StartWorker(void (CDV::*worker)(std::stop_token));
	void CreateMonitor(const CMediaType& type);

	std::atomic<State> m_state{Idle};
	HWND m_notifyWnd = nullptr;

	std::unique_ptr<CAVIJoiner> m_aviJoiner;
	std::unique_ptr<CAVIWriter> m_aviWriter; // only touched by the capture thread
	std::unique_ptr<CDVInput> m_dvInput;
	std::unique_ptr<CDVOutput> m_dvOutput;
	std::unique_ptr<CMonitor> m_monitor;
	std::unique_ptr<windv::FrameQueue> m_queue;
	std::jthread m_thread;

	std::mutex m_mutex; // guards m_target and m_error
	CaptureTarget m_target;
	CString m_error;

	std::atomic<long> m_dropped{0};
	std::atomic<long> m_counter{-1};
	std::atomic<REFERENCE_TIME> m_time{-1};
	std::atomic<REFERENCE_TIME> m_captureTime{0};
	std::atomic<std::time_t> m_dvTime{0};
};

std::vector<CString> GetVideoDeviceList();
