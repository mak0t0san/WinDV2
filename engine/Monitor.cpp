// Monitor.cpp : on-screen DV preview

#include "DShowBase.h"
#include "Monitor.h"

#include "ComApartment.h"
#include "DShowError.h"

namespace {

void SetDVDecoding(IGraphBuilder* graph, bool fullResolution)
{
	CComPtr<IBaseFilter> decoder;
	if (graph->FindFilterByName(L"DV Video Decoder", &decoder) != S_OK) {
		return;
	}
	if (CComQIPtr<IIPDVDec> dvDec = decoder) {
		dvDec->put_IPDisplay(fullResolution ? DVDECODERRESOLUTION_720x480 : DVDECODERRESOLUTION_360x240);
	}
}

} // namespace

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
	if (m_thread.joinable()) {
		m_thread.join();
	}

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
		std::scoped_lock lock(m_mutex);
		if (!m_sample || m_sampleFilled || frame.size() > static_cast<std::size_t>(m_sample->GetSize())) {
			return;
		}
		BYTE* data = nullptr;
		if (FAILED(m_sample->GetPointer(&data))) {
			return;
		}
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
			if (!filled) {
				return;
			}
		}
		lastDelivery = GetTickCount64();
		Deliver(sample);
	}
}
