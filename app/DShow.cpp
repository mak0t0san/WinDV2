// DShow.cpp : DirectShow pipeline - DV frame sources, sinks and the CDV controller

#include "stdafx.h"
#include "DShow.h"

#include "CaptureNaming.h"
#include "DVTimecode.h"
#include "TimeFormat.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {

constexpr std::size_t kQueueFrames = 100;
constexpr long kMinDVSampleSize = static_cast<long>(windv::kDVFrameSizePAL);

// Initializes COM on a worker thread for as long as the object lives.
class ComApartment {
public:
	ComApartment() : m_hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
	~ComApartment()
	{
		if (SUCCEEDED(m_hr))
			CoUninitialize();
	}
	ComApartment(const ComApartment&) = delete;
	ComApartment& operator=(const ComApartment&) = delete;

private:
	HRESULT m_hr;
};

CString FormatMessageWithResult(const CString& message, HRESULT hr)
{
	if (hr == S_OK)
		return message;

	WCHAR text[MAX_ERROR_TEXT_LEN] = L"";
	AMGetErrorTextW(hr, text, MAX_ERROR_TEXT_LEN);
	CString result;
	CString description(text);
	description.TrimRight();
	if (description.IsEmpty())
		result.Format(L"%s (0x%08lX)", message.GetString(), static_cast<unsigned long>(hr));
	else
		result.Format(L"%s (0x%08lX: %s)", message.GetString(), static_cast<unsigned long>(hr),
		              description.GetString());
	return result;
}

void SetDVDecoding(IGraphBuilder* graph, bool fullResolution)
{
	CComPtr<IBaseFilter> decoder;
	if (graph->FindFilterByName(L"DV Video Decoder", &decoder) != S_OK)
		return;
	if (CComQIPtr<IIPDVDec> dvDec = decoder)
		dvDec->put_IPDisplay(fullResolution ? DVDECODERRESOLUTION_720x480 : DVDECODERRESOLUTION_360x240);
}

// Calls visit(friendlyName, moniker, bindContext) for every video capture device
template <typename Visitor>
void ForEachVideoDevice(Visitor&& visit)
{
	CComPtr<ICreateDevEnum> devEnum;
	CheckHR(devEnum.CoCreateInstance(CLSID_SystemDeviceEnum), L"Can't create the system device enumerator");

	CComPtr<IEnumMoniker> monikers;
	const HRESULT hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &monikers, 0);
	CheckSucceeded(hr, L"Can't enumerate video devices");
	if (hr != S_OK) // S_FALSE: the category is empty
		return;

	CComPtr<IBindCtx> bindContext;
	CheckHR(CreateBindCtx(0, &bindContext), L"Can't create a bind context");

	CComPtr<IMoniker> moniker;
	while (monikers->Next(1, &moniker, nullptr) == S_OK) {
		CComPtr<IPropertyBag> bag;
		if (SUCCEEDED(moniker->BindToStorage(bindContext, nullptr, IID_PPV_ARGS(&bag)))) {
			CComVariant name;
			if (SUCCEEDED(bag->Read(L"FriendlyName", &name, nullptr)) && name.vt == VT_BSTR) {
				if (!visit(CString(name.bstrVal), moniker.p, bindContext.p))
					return;
			}
		}
		moniker.Release();
	}
}

CComPtr<IBaseFilter> FindVideoDevice(const CString& device)
{
	CComPtr<IBaseFilter> filter;
	ForEachVideoDevice([&](const CString& name, IMoniker* moniker, IBindCtx* bindContext) {
		if (name != device)
			return true;
		CheckHR(moniker->BindToObject(bindContext, nullptr, IID_PPV_ARGS(&filter)), L"Can't open the video device");
		return false;
	});
	if (!filter)
		throw DShowError(L"Video device \"" + device + L"\" not found", S_OK, DShowError::Cause::DeviceNotFound);
	return filter;
}

// Next free capture filename for base + formatted date (see CaptureNaming.h).
CString NextCaptureFilename(const CString& base, const CString& dtformat, int ndigits, std::time_t tim)
{
	if (tim <= 0)
		tim = std::time(nullptr);
	const std::wstring stem = windv::CaptureStem(base.GetString(), windv::FormatTime(dtformat.GetString(), tim));

	std::vector<std::wstring> existing;
	CFileFind finder;
	BOOL found = finder.FindFile(windv::CaptureSearchPattern(stem).c_str());
	while (found) {
		found = finder.FindNextFile();
		existing.emplace_back(finder.GetFileName().GetString());
	}
	return windv::NextCaptureFilename(stem, ndigits, existing).c_str();
}

} // namespace

/////////////////////////////////////////////////////////////////////////////
// Errors

DShowError::DShowError(const CString& message, HRESULT hr, Cause cause)
    : std::runtime_error(CStringA(FormatMessageWithResult(message, hr)).GetString()),
      m_message(FormatMessageWithResult(message, hr)),
      m_hr(hr),
      m_cause(cause)
{}

void CheckHR(HRESULT hr, LPCWSTR what)
{
	if (hr != S_OK)
		throw DShowError(what, hr);
}

void CheckSucceeded(HRESULT hr, LPCWSTR what)
{
	if (FAILED(hr))
		throw DShowError(what, hr);
}

std::vector<CString> GetVideoDeviceList()
{
	std::vector<CString> list;
	ForEachVideoDevice([&](const CString& name, IMoniker*, IBindCtx*) {
		list.push_back(name);
		return true;
	});
	return list;
}

/////////////////////////////////////////////////////////////////////////////
// CFilterGraph

CFilterGraph::CFilterGraph()
{
	CheckHR(m_GB.CoCreateInstance(CLSID_CaptureGraphBuilder2), L"Can't create CaptureGraphBuilder");
	CheckHR(m_FG.CoCreateInstance(CLSID_FilterGraph), L"Can't create FilterGraph");
	CheckHR(m_GB->SetFiltergraph(m_FG), L"Can't attach the filter graph");
	CheckHR(m_FG.QueryInterface(&m_MC), L"Can't get IMediaControl");
	CheckHR(m_FG.QueryInterface(&m_MS), L"Can't get IMediaSeeking");
	CheckHR(m_FG.QueryInterface(&m_ME), L"Can't get IMediaEventEx");
}

CFilterGraph::~CFilterGraph()
{
	if (m_MC)
		m_MC->Stop();
}

/////////////////////////////////////////////////////////////////////////////
// CInputGraph

class CInputGraph::CInputFilter : public CBaseFilter {
public:
	explicit CInputFilter(CInputGraph* graph)
	    : CBaseFilter(NAME("DV Destination"), nullptr, &m_lock, CLSID_NULL), m_graph(graph)
	{
		HRESULT hr = S_OK;
		m_input = std::make_unique<CInputPin>(this, &m_lock, &hr);
		CheckHR(hr, L"Can't create the input pin");
	}

	int GetPinCount() override { return 1; }
	CBasePin* GetPin(int n) override { return n == 0 ? m_input.get() : nullptr; }

	std::atomic<CInputGraph*> m_graph;

	class CInputPin : public CBaseInputPin {
	public:
		CInputPin(CInputFilter* filter, CCritSec* lock, HRESULT* phr)
		    : CBaseInputPin(NAME("Input"), filter, lock, phr, L"Input")
		{}

		HRESULT CheckMediaType(const CMediaType* pmt) override
		{
			return *pmt->Type() == MEDIATYPE_Interleaved ? S_OK : S_FALSE;
		}

		STDMETHODIMP Receive(IMediaSample* sample) override
		{
			HRESULT hr = CBaseInputPin::Receive(sample);
			if (hr != S_OK)
				return hr;

			CFrameHandler* handler = Handler();
			if (!handler)
				return S_OK;

			REFERENCE_TIME start = 0, end = 0;
			if (FAILED(sample->GetTime(&start, &end)))
				start = end = 0;
			BYTE* data = nullptr;
			if (FAILED(sample->GetPointer(&data)))
				return E_UNEXPECTED;
			const long length = sample->GetActualDataLength();

			// Exceptions must not cross the COM boundary into the upstream filter.
			try {
				handler->HandleFrame(end - start, {data, static_cast<std::size_t>(length)});
			} catch (...) {
				return E_FAIL;
			}
			return S_OK;
		}

		STDMETHODIMP EndOfStream() override
		{
			if (CFrameHandler* handler = Handler()) {
				try {
					handler->EndOfStream();
				} catch (...) {
					return E_FAIL;
				}
			}
			return S_OK;
		}

	private:
		CFrameHandler* Handler() const
		{
			CInputGraph* graph = static_cast<CInputFilter*>(m_pFilter)->m_graph.load();
			return graph ? graph->m_handler.load() : nullptr;
		}
	};

	std::unique_ptr<CInputPin> m_input;

private:
	CCritSec m_lock;
};

CInputGraph::CInputGraph()
{
	m_inputFilter = new CInputFilter(this);
	m_inputFilterRef = m_inputFilter;
	CheckHR(m_FG->AddFilter(m_inputFilterRef, L"InputFilter"), L"Can't add the input filter");
}

CInputGraph::~CInputGraph()
{
	Stop();
	m_inputFilter->m_graph = nullptr;
}

IPin* CInputGraph::InputPin() const
{
	return m_inputFilter->m_input.get();
}

bool CInputGraph::IsInputConnected() const
{
	return m_inputFilter->m_input->IsConnected() != FALSE;
}

void CInputGraph::Run(CFrameHandler* handler)
{
	m_handler = handler;
	CheckSucceeded(m_MC->Run(), L"Can't start the source graph");
}

void CInputGraph::Stop()
{
	if (m_MC)
		m_MC->Stop();
	m_handler = nullptr;
}

void CInputGraph::GetMediaType(CMediaType* type)
{
	AM_MEDIA_TYPE mt{};
	CheckHR(m_inputFilter->m_input->ConnectionMediaType(&mt), L"The source is not connected");
	*type = mt;
	FreeMediaType(mt);
	if (type->GetSampleSize() < kMinDVSampleSize)
		type->SetSampleSize(kMinDVSampleSize);
}

/////////////////////////////////////////////////////////////////////////////
// COutputGraph

class COutputGraph::COutputFilter : public CBaseFilter {
public:
	explicit COutputFilter(COutputGraph* graph)
	    : CBaseFilter(NAME("DV Source"), nullptr, &m_lock, CLSID_NULL), m_graph(graph)
	{
		HRESULT hr = S_OK;
		m_output = std::make_unique<COutputPin>(this, &m_lock, &hr);
		CheckHR(hr, L"Can't create the output pin");
	}

	int GetPinCount() override { return 1; }
	CBasePin* GetPin(int n) override { return n == 0 ? m_output.get() : nullptr; }

	COutputGraph* m_graph;

	class COutputPin : public CBaseOutputPin {
	public:
		COutputPin(COutputFilter* filter, CCritSec* lock, HRESULT* phr)
		    : CBaseOutputPin(NAME("Output"), filter, lock, phr, L"Output")
		{}

		HRESULT GetMediaType(int position, CMediaType* pmt) override
		{
			if (position < 0)
				return E_INVALIDARG;
			if (position > 0)
				return VFW_S_NO_MORE_ITEMS;
			*pmt = Graph()->m_type;
			return S_OK;
		}

		HRESULT CheckMediaType(const CMediaType* pmt) override { return *pmt == Graph()->m_type ? S_OK : S_FALSE; }

		HRESULT DecideBufferSize(IMemAllocator* allocator, ALLOCATOR_PROPERTIES* request) override
		{
			const long sampleSize = static_cast<long>(Graph()->m_type.GetSampleSize());
			request->cbAlign = (std::max)(request->cbAlign, 1L);
			request->cbBuffer = (std::max)(request->cbBuffer, sampleSize);
			request->cBuffers = (std::max)(request->cBuffers, static_cast<long>((std::max)(Graph()->m_queueDepth, 1)));

			ALLOCATOR_PROPERTIES actual{};
			const HRESULT hr = allocator->SetProperties(request, &actual);
			if (FAILED(hr))
				return hr;
			return actual.cbBuffer < sampleSize ? E_FAIL : S_OK;
		}

		HRESULT Deliver(IMediaSample* sample) override
		{
			if (m_queue) {
				sample->AddRef(); // COutputQueue::Receive takes over a reference
				return m_queue->Receive(sample);
			}
			return CBaseOutputPin::Deliver(sample);
		}

		HRESULT DeliverEndOfStream() override
		{
			if (m_queue) {
				m_queue->EOS();
				return S_OK;
			}
			return CBaseOutputPin::DeliverEndOfStream();
		}

		HRESULT Active() override
		{
			const int depth = Graph()->m_queueDepth;
			if (depth > 0) {
				HRESULT hr = S_OK;
				m_queue = std::make_unique<COutputQueue>(GetConnected(), &hr, FALSE, TRUE, 1, FALSE, depth,
				                                         THREAD_PRIORITY_ABOVE_NORMAL);
				if (FAILED(hr)) {
					m_queue.reset();
					return hr;
				}
			}
			return CBaseOutputPin::Active();
		}

		HRESULT Inactive() override
		{
			m_queue.reset();
			return CBaseOutputPin::Inactive();
		}

	private:
		COutputGraph* Graph() const { return static_cast<COutputFilter*>(m_pFilter)->m_graph; }

		std::unique_ptr<COutputQueue> m_queue;
	};

	std::unique_ptr<COutputPin> m_output;

private:
	CCritSec m_lock;
};

COutputGraph::COutputGraph(const CMediaType& type, int queueDepth) : m_type(type), m_queueDepth(queueDepth)
{
	m_outputFilter = new COutputFilter(this);
	m_outputFilterRef = m_outputFilter;
	CheckHR(m_FG->AddFilter(m_outputFilterRef, L"OutputFilter"), L"Can't add the output filter");
}

COutputGraph::~COutputGraph()
{
	if (m_MC)
		m_MC->Stop();
}

HRESULT COutputGraph::GetDeliveryBuffer(IMediaSample** sample)
{
	return m_outputFilter->m_output->GetDeliveryBuffer(sample, nullptr, nullptr, 0);
}

HRESULT COutputGraph::Deliver(IMediaSample* sample)
{
	return m_outputFilter->m_output->Deliver(sample);
}

void COutputGraph::DeliverEndOfStream()
{
	m_outputFilter->m_output->DeliverEndOfStream();
}

void COutputGraph::WaitForCompletion()
{
	long evCode = 0;
	m_ME->WaitForCompletion(5000, &evCode);
}

void COutputGraph::HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame)
{
	CComPtr<IMediaSample> sample;
	CheckHR(GetDeliveryBuffer(&sample), L"Can't get an output buffer");
	if (frame.size() > static_cast<std::size_t>(sample->GetSize()))
		throw DShowError(L"DV frame is larger than the output buffer");

	BYTE* data = nullptr;
	CheckHR(sample->GetPointer(&data), L"Can't access the output buffer");
	std::copy(frame.begin(), frame.end(), data);
	sample->SetActualDataLength(static_cast<long>(frame.size()));

	REFERENCE_TIME end = m_time + duration;
	if (duration)
		sample->SetTime(&m_time, &end);
	m_time = end;
	sample->SetSyncPoint(TRUE);

	Deliver(sample);
}

/////////////////////////////////////////////////////////////////////////////
// CAVIReader

CAVIReader::CAVIReader(const CString& filename)
{
	CComPtr<IBaseFilter> source;
	CheckSucceeded(m_FG->AddSourceFilter(filename, L"File source", &source), L"Can't open " + filename);

	CComPtr<IBaseFilter> splitter;
	CheckHR(splitter.CoCreateInstance(CLSID_AviSplitter), L"Can't create the AVI splitter");
	CheckHR(m_FG->AddFilter(splitter, L"AVI Splitter"), L"Can't add the AVI splitter");
	CheckHR(m_GB->RenderStream(nullptr, nullptr, source, nullptr, splitter), filename + L" is not an AVI file");

	// Type-1 AVI: the interleaved DV stream connects directly.
	const HRESULT hr = m_GB->RenderStream(nullptr, &MEDIATYPE_Interleaved, splitter, nullptr, m_inputFilterRef);
	if (hr != S_OK || !IsInputConnected()) {
		// Type-2 AVI: separate video and audio streams go back through the DV muxer.
		CComPtr<IBaseFilter> muxer;
		CheckHR(muxer.CoCreateInstance(CLSID_DVMux), L"Can't create the DV muxer");
		CheckHR(m_FG->AddFilter(muxer, L"DV muxer"), L"Can't add the DV muxer");
		CheckSucceeded(m_GB->RenderStream(nullptr, &MEDIATYPE_Video, splitter, nullptr, muxer),
		               filename + L" has no DV video stream");
		m_GB->RenderStream(nullptr, &MEDIATYPE_Audio, splitter, nullptr, muxer); // audio is optional
		CheckSucceeded(m_GB->RenderStream(nullptr, nullptr, muxer, nullptr, m_inputFilterRef),
		               filename + L" is not a DV AVI file");
	}
	if (!IsInputConnected())
		throw DShowError(filename + L" is not a DV AVI file");
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
}

/////////////////////////////////////////////////////////////////////////////
// CAVIJoiner

CAVIJoiner::CAVIJoiner(const CString& filenames)
{
	int pos = 0;
	while (pos >= 0) {
		CString pattern = filenames.Tokenize(L"|", pos);
		if (pos < 0)
			break;
		pattern.Trim();
		if (pattern.IsEmpty())
			continue;

		std::vector<CString> matches;
		CFileFind finder;
		BOOL found = finder.FindFile(pattern);
		if (!found)
			throw DShowError(pattern + L": file not found");
		while (found) {
			found = finder.FindNextFile();
			if (!finder.IsDirectory())
				matches.push_back(finder.GetFilePath());
		}
		std::ranges::sort(matches, [](const CString& a, const CString& b) { return a.CompareNoCase(b) < 0; });
		m_filenames.insert(m_filenames.end(), matches.begin(), matches.end());
	}

	if (m_filenames.empty())
		throw DShowError(L"No file selected");
	m_reader = std::make_unique<CAVIReader>(m_filenames[m_next++]);
}

CAVIJoiner::~CAVIJoiner()
{
	Stop();
}

void CAVIJoiner::GetMediaType(CMediaType* type)
{
	m_reader->GetMediaType(type);
}

void CAVIJoiner::Run(CFrameHandler* handler)
{
	m_handler = handler;
	m_thread = std::jthread([this](std::stop_token stop) { JoinerThread(stop); });
	m_reader->Run(this);
}

void CAVIJoiner::Stop()
{
	m_stopping = true;
	if (m_thread.joinable()) {
		m_thread.request_stop();
		m_thread.join();
	}
	m_reader.reset();
	m_stopping = false;
	m_handler = nullptr;
}

void CAVIJoiner::HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame)
{
	if (m_stopping)
		return;
	if (CFrameHandler* handler = m_handler.load())
		handler->HandleFrame(duration, frame);
}

void CAVIJoiner::EndOfStream()
{
	{
		std::lock_guard lock(m_mutex);
		m_readerEndedFlag = true;
	}
	m_readerEnded.notify_one();
}

void CAVIJoiner::JoinerThread(std::stop_token stop)
{
	ComApartment com;
	try {
		for (;;) {
			{
				std::unique_lock lock(m_mutex);
				if (!m_readerEnded.wait(lock, stop, [this] { return m_readerEndedFlag; }))
					return; // stop requested
				m_readerEndedFlag = false;
			}

			// The finished reader is destroyed here rather than on its own
			// streaming thread, which would deadlock stopping its graph.
			m_reader.reset();
			if (m_next >= m_filenames.size()) {
				if (CFrameHandler* handler = m_handler.load())
					handler->EndOfStream();
				return;
			}
			m_reader = std::make_unique<CAVIReader>(m_filenames[m_next++]);
			m_reader->Run(this);
		}
	} catch (const DShowError& e) {
		if (CFrameHandler* handler = m_handler.load())
			handler->SourceError(e.Message());
	}
}

/////////////////////////////////////////////////////////////////////////////
// CDVControl

void CDVControl::CtrlAttach(IUnknown* device)
{
	m_ET.Release();
	device->QueryInterface(IID_PPV_ARGS(&m_ET)); // optional: not every device has transport control
}

void CDVControl::CtrlStop()
{
	if (m_ET)
		m_ET->put_Mode(ED_MODE_STOP);
}

void CDVControl::CtrlPlay()
{
	if (m_ET)
		m_ET->put_Mode(ED_MODE_PLAY);
}

void CDVControl::CtrlPause()
{
	if (m_ET) {
		m_ET->put_Mode(ED_MODE_PLAY);
		m_ET->put_Mode(ED_MODE_FREEZE);
	}
}

void CDVControl::CtrlRecord()
{
	if (m_ET)
		m_ET->put_Mode(ED_MODE_RECORD);
}

void CDVControl::CtrlRecPause()
{
	if (m_ET)
		m_ET->put_Mode(ED_MODE_RECORD_FREEZE);
}

/////////////////////////////////////////////////////////////////////////////
// CDVInput

CDVInput::CDVInput(const CString& device)
{
	CComPtr<IBaseFilter> source = FindVideoDevice(device);
	CtrlAttach(source);
	CheckHR(m_FG->AddFilter(source, L"DVin"), L"Can't add the DV device to the graph");
	CheckHR(m_GB->RenderStream(nullptr, &MEDIATYPE_Interleaved, source, nullptr, m_inputFilterRef),
	        L"Can't connect to the DV device (is another program using it?)");

	CComPtr<IPin> devicePin;
	CheckHR(InputPin()->ConnectedTo(&devicePin), L"Can't find the DV output pin");
	CheckHR(devicePin.QueryInterface(&m_DF), L"Can't find IAMDroppedFrames");
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
}

long CDVInput::GetDroppedFrames()
{
	long dropped = -1;
	m_DF->GetNumDropped(&dropped);
	return dropped;
}

/////////////////////////////////////////////////////////////////////////////
// CDVOutput

CDVOutput::CDVOutput(const CString& device, const CMediaType& type) : COutputGraph(type, 10)
{
	CComPtr<IBaseFilter> sink = FindVideoDevice(device);
	CtrlAttach(sink);
	CheckHR(m_FG->AddFilter(sink, L"DVout"), L"Can't add the DV device to the graph");
	CheckHR(m_GB->RenderStream(nullptr, nullptr, m_outputFilterRef, nullptr, sink),
	        L"Can't connect to the DV device (is another program using it?)");
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
	if (m_MC->Run() != S_OK) {
		OAFilterState state = State_Stopped;
		CheckHR(m_MC->GetState(1000, &state), L"Can't start DV output");
		if (state != State_Running)
			throw DShowError(L"DV output not running");
	}
}

CDVOutput::~CDVOutput()
{
	DeliverEndOfStream();
	WaitForCompletion();
}

/////////////////////////////////////////////////////////////////////////////
// CAVIWriter

CAVIWriter::CAVIWriter(const CString& base, const CString& dtformat, int ndigits, std::time_t dvTime, bool type2AVI,
                       const CMediaType& type)
    : COutputGraph(type), m_dvTime(dvTime), m_base(base), m_dtformat(dtformat), m_ndigits(ndigits)
{
	m_tmpfile = NextCaptureFilename(m_base, L"~" + m_dtformat, m_ndigits, m_dvTime);

	CComPtr<IBaseFilter> mux;
	CComPtr<IFileSinkFilter> sink;
	CheckHR(m_GB->SetOutputFileName(&MEDIASUBTYPE_Avi, m_tmpfile, &mux, &sink), L"Can't create " + m_tmpfile);
	if (CComQIPtr<IFileSinkFilter2> sink2 = sink)
		sink2->SetMode(AM_FILE_OVERWRITE);

	if (type2AVI) {
		CComPtr<IBaseFilter> splitter;
		CheckHR(splitter.CoCreateInstance(CLSID_DVSplitter), L"Can't create the DV splitter");
		CheckHR(m_FG->AddFilter(splitter, L"DV splitter"), L"Can't add the DV splitter");
		CheckHR(m_GB->RenderStream(nullptr, &MEDIATYPE_Interleaved, m_outputFilterRef, nullptr, splitter),
		        L"Can't connect the DV splitter");
		CheckHR(m_GB->RenderStream(nullptr, &MEDIATYPE_Video, splitter, nullptr, mux),
		        L"Can't connect the video stream to the AVI writer");
		CheckHR(m_GB->RenderStream(nullptr, &MEDIATYPE_Audio, splitter, nullptr, mux),
		        L"Can't connect the audio stream to the AVI writer");
	} else {
		CheckHR(m_GB->RenderStream(nullptr, &MEDIATYPE_Interleaved, m_outputFilterRef, nullptr, mux),
		        L"Can't connect the AVI writer");
	}
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
	CheckSucceeded(m_MC->Run(), L"Can't start writing " + m_tmpfile);
}

CAVIWriter::~CAVIWriter()
{
	if (m_finished)
		return;
	try {
		Finish();
	} catch (const DShowError& e) {
		TRACE(L"%s\n", e.Message().GetString());
	}
}

void CAVIWriter::Finish()
{
	if (m_finished)
		return;
	m_finished = true;

	DeliverEndOfStream();
	WaitForCompletion();
	m_MC->Stop();

	const CString filename = NextCaptureFilename(m_base, m_dtformat, m_ndigits, m_dvTime);
	if (!MoveFileEx(m_tmpfile, filename, 0))
		throw DShowError(L"Can't rename " + m_tmpfile + L" to " + filename, HRESULT_FROM_WIN32(GetLastError()));
}

/////////////////////////////////////////////////////////////////////////////
// CMonitor

CMonitor::CMonitor(HWND hWnd, const CMediaType& type) : COutputGraph(type), m_hWnd(hWnd)
{
	CheckHR(m_FG.QueryInterface(&m_VW), L"Can't get IVideoWindow");
	CheckSucceeded(m_GB->RenderStream(nullptr, nullptr, m_outputFilterRef, nullptr, nullptr),
	               L"Can't build the preview (is a DV decoder installed?)");
	SetDVDecoding(m_FG, false);
	m_VW->put_Owner(reinterpret_cast<OAHWND>(m_hWnd));
	m_VW->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	Resize();
	CheckSucceeded(m_MC->Run(), L"Can't start the preview");

	m_thread = std::jthread([this](std::stop_token stop) { MonitoringThread(stop); });
}

CMonitor::~CMonitor()
{
	m_thread.request_stop();
	// Stopping the graph decommits the allocator, which releases the thread if
	// it is blocked in GetDeliveryBuffer.
	m_MC->Stop();
	if (m_thread.joinable())
		m_thread.join();

	m_VW->put_Visible(OAFALSE);
	m_VW->put_Owner(0);
}

void CMonitor::Resize()
{
	RECT rect{};
	::GetClientRect(m_hWnd, &rect);
	const long cx = rect.right - rect.left;
	const long cy = rect.bottom - rect.top;

	// Largest 4:3 rectangle, centred.
	const long w = (std::min)(cx, cy * 4 / 3);
	const long h = (std::min)(cy, cx * 3 / 4);
	m_VW->SetWindowPosition((cx - w) / 2, (cy - h) / 2, w, h);
}

void CMonitor::HandleFrame(REFERENCE_TIME /*duration*/, std::span<const BYTE> frame)
{
	{
		std::lock_guard lock(m_mutex);
		if (!m_sample || m_sampleFilled || frame.size() > static_cast<std::size_t>(m_sample->GetSize()))
			return;
		BYTE* data = nullptr;
		if (FAILED(m_sample->GetPointer(&data)))
			return;
		std::copy(frame.begin(), frame.end(), data);
		m_sample->SetActualDataLength(static_cast<long>(frame.size()));
		m_sample->SetSyncPoint(TRUE);
		m_sampleFilled = true;
	}
	m_filled.notify_one();
}

void CMonitor::MonitoringThread(std::stop_token stop)
{
	ComApartment com;
	std::mutex sleepMutex;
	std::condition_variable_any sleeper;
	const auto sleepFor = [&](ULONGLONG ms) {
		std::unique_lock lock(sleepMutex);
		sleeper.wait_for(lock, stop, std::chrono::milliseconds(ms), [] { return false; });
	};

	ULONGLONG lastDelivery = GetTickCount64();
	while (!stop.stop_requested()) {
		CComPtr<IMediaSample> sample;
		if (GetDeliveryBuffer(&sample) != S_OK) {
			sleepFor(100);
			continue;
		}

		// Preview at most as often as the previous frame took to go through, so
		// a slow renderer drops frames instead of holding up capture.
		const ULONGLONG elapsed = GetTickCount64() - lastDelivery;
		sleepFor(elapsed < 200 ? elapsed + 10 : 200);

		{
			std::unique_lock lock(m_mutex);
			m_sample = sample;
			m_sampleFilled = false;
			const bool filled = m_filled.wait(lock, stop, [this] { return m_sampleFilled; });
			m_sample.Release();
			if (!filled)
				return;
		}
		lastDelivery = GetTickCount64();
		Deliver(sample);
	}
}

/////////////////////////////////////////////////////////////////////////////
// CDV

BEGIN_MESSAGE_MAP(CDV, CStatic)
	ON_WM_SIZE()
END_MESSAGE_MAP()

CDV::CDV() = default;

CDV::~CDV()
{
	Destroy();
}

void CDV::OnSize(UINT nType, int cx, int cy)
{
	CStatic::OnSize(nType, cx, cy);
	if (m_monitor)
		m_monitor->Resize();
}

std::size_t CDV::GetQueueLoad() const
{
	return m_queue ? m_queue->Load() : 0;
}

CString CDV::TakeError()
{
	std::lock_guard lock(m_mutex);
	CString error = m_error;
	m_error.Empty();
	return error;
}

void CDV::ReportError(const CString& message)
{
	{
		std::lock_guard lock(m_mutex);
		if (!m_error.IsEmpty())
			return; // keep the first error; later ones are usually consequences
		m_error = message;
	}
	if (m_notifyWnd)
		::PostMessage(m_notifyWnd, WM_DV_ERROR, 0, 0);
}

void CDV::NotifyTimeChange(std::time_t dvTime)
{
	m_dvTime = dvTime;
	if (m_notifyWnd)
		::PostMessage(m_notifyWnd, WM_DV_TIMECHANGE, 0, 0);
}

void CDV::Destroy()
{
	m_state = Idle;

	// Wake everything that might be blocked on the queue or a source, then join.
	if (m_queue)
		m_queue->Close();
	if (m_aviJoiner)
		m_aviJoiner->Stop();
	if (m_dvInput)
		m_dvInput->Stop();
	if (m_thread.joinable()) {
		m_thread.request_stop();
		m_thread.join();
	}

	m_aviJoiner.reset();
	if (m_dvInput) {
		if (m_DVctrl)
			m_dvInput->CtrlStop();
		m_dvInput.reset();
	}
	m_aviWriter.reset();
	if (m_dvOutput) {
		if (m_DVctrl)
			m_dvOutput->CtrlStop();
		m_dvOutput.reset();
	}
	m_monitor.reset();
	m_queue.reset();

	m_dropped = 0;
	m_counter = -1;
	m_time = -1;
	m_captureTime = 0;
}

void CDV::HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame)
{
	try {
		m_queue->Put(duration, frame);
	} catch (const std::length_error&) {
		ReportError(L"Received a DV frame larger than expected");
		m_queue->Close();
	}
}

void CDV::EndOfStream()
{
	m_queue->Close();
}

void CDV::SourceError(const CString& message)
{
	ReportError(message);
	m_queue->Close();
}

// The preview is optional: without a working DV decoder, capture and record
// still run, just without a picture.
void CDV::CreateMonitor(const CMediaType& type)
{
	try {
		m_monitor = std::make_unique<CMonitor>(m_hWnd, type);
	} catch (const DShowError& e) {
		TRACE(L"Preview disabled: %s\n", e.Message().GetString());
		m_monitor.reset();
	}
}

void CDV::StartWorker(void (CDV::*worker)(std::stop_token))
{
	m_thread = std::jthread([this, worker](std::stop_token stop) { (this->*worker)(stop); });
}

void CDV::BuildCapturing(const CString& device)
{
	Destroy();
	TakeError();
	m_notifyWnd = ::GetParent(m_hWnd);

	m_dvInput = std::make_unique<CDVInput>(device);

	CMediaType type;
	m_dvInput->GetMediaType(&type);

	CreateMonitor(type);
	m_queue = std::make_unique<windv::FrameQueue>(kQueueFrames, type.GetSampleSize());

	m_state = CapturePaused;
	m_dvInput->Run(this);
	StartWorker(&CDV::CapturingThread);

	InvalidateRect(nullptr);
	UpdateWindow();
}

void CDV::StopCapturing()
{
	State expected = Capturing;
	if (m_state.compare_exchange_strong(expected, CapturePaused) && m_DVctrl)
		m_dvInput->CtrlPause();
}

void CDV::StartCapturing(const CString& filename, const CString& dtformat, int ndigits, REFERENCE_TIME captureTime)
{
	if (m_state != CapturePaused)
		return;
	{
		std::lock_guard lock(m_mutex);
		m_target = {filename, dtformat, ndigits};
	}
	m_captureTime = captureTime;
	m_state = Capturing;
	if (m_DVctrl)
		m_dvInput->CtrlPlay();
}

void CDV::BuildRecording(const CString& filenames, const CString& device)
{
	Destroy();
	TakeError();
	m_notifyWnd = ::GetParent(m_hWnd);

	m_aviJoiner = std::make_unique<CAVIJoiner>(filenames);

	CMediaType type;
	m_aviJoiner->GetMediaType(&type);

	CreateMonitor(type);
	m_dvOutput = std::make_unique<CDVOutput>(device, type);
	m_queue = std::make_unique<windv::FrameQueue>(kQueueFrames, type.GetSampleSize());

	m_state = RecordPaused;
	m_aviJoiner->Run(this);
	if (m_DVctrl)
		m_dvOutput->CtrlRecPause();
	StartWorker(&CDV::RecordingThread);

	InvalidateRect(nullptr);
	UpdateWindow();
}

void CDV::StopRecording()
{
	State expected = Recording;
	if (m_state.compare_exchange_strong(expected, RecordPaused) && m_DVctrl)
		m_dvOutput->CtrlRecPause();
}

void CDV::StartRecording()
{
	State expected = RecordPaused;
	if (m_state.compare_exchange_strong(expected, Recording) && m_DVctrl)
		m_dvOutput->CtrlRecord();
}

void CDV::CapturingThread(std::stop_token /*stop*/)
{
	ComApartment com;
	try {
		CaptureLoop();
		FinishWriter();
	} catch (const DShowError& e) {
		ReportError(e.Message());
	} catch (const std::exception& e) {
		ReportError(CString(e.what()));
	}
	m_aviWriter.reset();
	NotifyTimeChange(0);
}

void CDV::FinishWriter()
{
	if (auto writer = std::move(m_aviWriter))
		writer->Finish();
}

void CDV::CaptureLoop()
{
	CMediaType type;
	m_dvInput->GetMediaType(&type);

	long nFrames = 0;
	long counter = 0;
	long dropped = m_dvInput->GetDroppedFrames();
	std::time_t dvTime = 0, lastValidDVTime = 0;
	m_counter = 0;
	m_time = 0;

	while (m_state != Idle) {
		const auto frame = m_queue->Get();
		if (!frame)
			break;

		// Track the camcorder's recording timestamp; a jump means a new scene.
		const std::time_t newDVTime = windv::GetDVRecordingTime(frame->data).value_or(0);
		std::time_t deltaDVTime = 0;
		if (newDVTime != dvTime) {
			if (newDVTime > 0) {
				if (lastValidDVTime > 0)
					deltaDVTime =
					    newDVTime > lastValidDVTime ? newDVTime - lastValidDVTime : lastValidDVTime - newDVTime;
				lastValidDVTime = newDVTime;
			}
			dvTime = newDVTime;
			NotifyTimeChange(dvTime);
		}

		// Only preview while the queue is draining comfortably.
		if (m_monitor && m_queue->Load() < m_queue->Capacity() / 2)
			m_monitor->HandleFrame(frame->duration, frame->data);

		if (m_state == Capturing) {
			const int threshold = m_discontinuityThreshold;
			if (m_aviWriter && (nFrames >= m_maxAVIFrames || (threshold > 0 && deltaDVTime > threshold)))
				FinishWriter();
			if (!m_aviWriter) {
				CaptureTarget target;
				{
					std::lock_guard lock(m_mutex);
					target = m_target;
				}
				m_aviWriter = std::make_unique<CAVIWriter>(target.filename, target.dtformat, target.ndigits, dvTime,
				                                           m_type2AVI, type);
				nFrames = 0;
				counter = 0;
			}
			if (counter % (std::max)(m_everyNth.load(), 1) == 0) {
				m_aviWriter->HandleFrame(frame->duration, frame->data);
				++nFrames;
			}
			if (dvTime > 0 && m_aviWriter->m_dvTime <= 0)
				m_aviWriter->m_dvTime = dvTime;
			++counter;
			++m_counter;
			m_time += frame->duration;

			const REFERENCE_TIME captureTime = m_captureTime;
			if (captureTime && m_time >= captureTime) {
				m_captureTime = 0;
				State expected = Capturing;
				m_state.compare_exchange_strong(expected, Finished);
			}
			m_dropped = m_dvInput->GetDroppedFrames() - dropped;
		} else {
			FinishWriter();
			dropped = m_dvInput->GetDroppedFrames();
			if (m_state != Finished) {
				m_dropped = 0;
				m_counter = 0;
				m_time = 0;
			}
		}
	}
}

void CDV::RecordingThread(std::stop_token /*stop*/)
{
	ComApartment com;
	NotifyTimeChange(0);
	try {
		RecordLoop();
	} catch (const DShowError& e) {
		ReportError(e.Message());
	} catch (const std::exception& e) {
		ReportError(CString(e.what()));
	}
	NotifyTimeChange(0);
}

void CDV::RecordLoop()
{
	auto frame = m_queue->Get();
	if (!frame)
		return;

	m_counter = 0;
	m_time = 0;
	std::time_t dvTime = 0;
	// While paused or finished the current frame is sent again and again, which
	// keeps a still picture on the DV output. The output queue paces the loop.
	while (m_state != Idle) {
		const std::time_t newDVTime = windv::GetDVRecordingTime(frame->data).value_or(0);
		if (newDVTime != dvTime) {
			dvTime = newDVTime;
			NotifyTimeChange(dvTime);
		}
		if (m_monitor && m_recordPreview && (m_queue->IsClosed() || m_queue->Load() > m_queue->Capacity() / 2))
			m_monitor->HandleFrame(frame->duration, frame->data);

		m_dvOutput->HandleFrame(frame->duration, frame->data);

		if (m_state == Recording) {
			++m_counter;
			m_time += frame->duration;
			if (auto next = m_queue->Get()) {
				frame = next;
			} else {
				State expected = Recording;
				m_state.compare_exchange_strong(expected, Finished);
			}
		}
	}
}
