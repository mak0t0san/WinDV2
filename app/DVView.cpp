// DVView.cpp : the preview window, wrapping the DirectShow engine for the MFC UI

#include "stdafx.h"
#include "DVView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CDV, CStatic)
	ON_WM_SIZE()
END_MESSAGE_MAP()

CDV::CDV() : DVEngine(this)
{}

CDV::~CDV()
{
	// Join the workers while this object, which receives their events, is intact.
	Destroy();
}

void CDV::OnSize(UINT nType, int cx, int cy)
{
	CStatic::OnSize(nType, cx, cy);
	ResizePreview();
}

// Called on worker threads: MFC handle maps are per thread, so only raw
// ::PostMessage is safe here.
void CDV::OnDVTimeChanged()
{
	if (m_notifyWnd) {
		::PostMessage(m_notifyWnd, WM_DV_TIMECHANGE, 0, 0);
	}
}

void CDV::OnError()
{
	if (m_notifyWnd) {
		::PostMessage(m_notifyWnd, WM_DV_ERROR, 0, 0);
	}
}

void CDV::AttachWindows()
{
	m_notifyWnd = ::GetParent(m_hWnd);
	SetPreviewWindow(m_hWnd);
}

void CDV::BuildCapturing(const std::wstring& device)
{
	AttachWindows();
	DVEngine::BuildCapturing(device);
	InvalidateRect(nullptr);
	UpdateWindow();
}

void CDV::BuildRecording(const std::wstring& filenames, const std::wstring& device)
{
	AttachWindows();
	DVEngine::BuildRecording(filenames, device);
	InvalidateRect(nullptr);
	UpdateWindow();
}
