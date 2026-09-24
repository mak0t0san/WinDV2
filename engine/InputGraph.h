// InputGraph.h : a graph that ends in our own input pin
#pragma once

#include "DShowBase.h"
#include "FilterGraph.h"
#include "FrameInterfaces.h"

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
