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

	// Throws DShowError if a filter downstream has reported an error (e.g. the
	// file writer on a full disk). HandleFrame calls it about once a second.
	void CheckForErrors();

	// What HandleFrame's errors say failed, e.g. "Can't write D:\dv\~tape.avi".
	std::wstring m_failureMessage = L"The output stopped with an error";

private:
	CMediaType m_type;
	REFERENCE_TIME m_time = 0;
	int m_queueDepth;
	unsigned m_framesSinceCheck = 0;
};
