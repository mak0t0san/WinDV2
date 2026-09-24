// InputGraph.cpp : a graph that ends in our own input pin

#include "DShowBase.h"
#include "InputGraph.h"

#include "DShowError.h"
#include "DVTimecode.h"

namespace {

constexpr long kMinDVSampleSize = static_cast<long>(windv::kDVFrameSizePAL);

} // namespace

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
			if (hr != S_OK) {
				return hr;
			}

			CFrameHandler* handler = Handler();
			if (!handler) {
				return S_OK;
			}

			REFERENCE_TIME start = 0, end = 0;
			if (FAILED(sample->GetTime(&start, &end))) {
				start = end = 0;
			}
			BYTE* data = nullptr;
			if (FAILED(sample->GetPointer(&data))) {
				return E_UNEXPECTED;
			}
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
	if (m_MC) {
		m_MC->Stop();
	}
	m_handler = nullptr;
}

void CInputGraph::GetMediaType(CMediaType* type)
{
	AM_MEDIA_TYPE mt{};
	CheckHR(m_inputFilter->m_input->ConnectionMediaType(&mt), L"The source is not connected");
	*type = mt;
	FreeMediaType(mt);
	if (type->GetSampleSize() < kMinDVSampleSize) {
		type->SetSampleSize(kMinDVSampleSize);
	}
}
