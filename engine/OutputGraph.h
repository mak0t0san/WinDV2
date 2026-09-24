// OutputGraph.h : a graph that starts at our own output pin
#pragma once

#include "DShowBase.h"
#include "FilterGraph.h"
#include "FrameInterfaces.h"

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
