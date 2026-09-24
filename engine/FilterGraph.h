// FilterGraph.h : a DirectShow filter graph with a capture graph builder
#pragma once

#include "DShowBase.h"

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
