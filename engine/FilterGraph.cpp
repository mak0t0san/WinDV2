// FilterGraph.cpp : a DirectShow filter graph with a capture graph builder

#include "DShowBase.h"
#include "FilterGraph.h"

#include "DShowError.h"

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
	if (m_MC) {
		m_MC->Stop();
	}
}
