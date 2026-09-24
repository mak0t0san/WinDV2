// OutputGraph.cpp : a graph that starts at our own output pin

#include "DShowBase.h"
#include "OutputGraph.h"

#include "DShowError.h"

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
			if (position < 0) {
				return E_INVALIDARG;
			}
			if (position > 0) {
				return VFW_S_NO_MORE_ITEMS;
			}
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
			if (FAILED(hr)) {
				return hr;
			}
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
	if (m_MC) {
		m_MC->Stop();
	}
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
	if (frame.size() > static_cast<std::size_t>(sample->GetSize())) {
		throw DShowError(L"DV frame is larger than the output buffer");
	}

	BYTE* data = nullptr;
	CheckHR(sample->GetPointer(&data), L"Can't access the output buffer");
	std::copy(frame.begin(), frame.end(), data);
	sample->SetActualDataLength(static_cast<long>(frame.size()));

	REFERENCE_TIME end = m_time + duration;
	if (duration) {
		sample->SetTime(&m_time, &end);
	}
	m_time = end;
	sample->SetSyncPoint(TRUE);

	CheckSucceeded(Deliver(sample), m_failureMessage);
	if (++m_framesSinceCheck >= 25) {
		m_framesSinceCheck = 0;
		CheckForErrors();
	}
}

void COutputGraph::CheckForErrors()
{
	long code = 0;
	LONG_PTR param1 = 0, param2 = 0;
	while (m_ME->GetEvent(&code, &param1, &param2, 0) == S_OK) {
		const HRESULT hr = static_cast<HRESULT>(param1);
		m_ME->FreeEventParams(code, param1, param2);
		switch (code) {
		case EC_ERRORABORT:
		case EC_ERRORABORTEX:
		case EC_STREAM_ERROR_STOPPED:
			throw DShowError(m_failureMessage, FAILED(hr) ? hr : E_FAIL);
		default:
			break;
		}
	}
}
