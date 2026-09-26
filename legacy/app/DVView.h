// DVView.h : the preview window, wrapping the DirectShow engine for the MFC UI
#pragma once

#include "DShowError.h"
#include "DVDevice.h"
#include "DVEngine.h"

// Posted to the CDV's parent when the DV recording timestamp changes; read the
// new value with GetDVTime().
constexpr UINT WM_DV_TIMECHANGE = WM_USER + 201;
// Posted to the CDV's parent when a worker thread fails; fetch the message with
// TakeError().
constexpr UINT WM_DV_ERROR = WM_USER + 202;

// The black preview area. It is also the engine: the dialog drives capture and
// record through the DVEngine methods, and engine events come back as
// WM_DV_* messages posted to the parent.
class CDV : public CStatic, private DVEngineEvents, public DVEngine {
public:
	CDV();
	~CDV() override;

	// These set the preview and notification windows, then build the pipeline.
	void BuildCapturing(const std::wstring& device);
	void BuildRecording(const std::wstring& filenames, const std::wstring& device);

protected:
	afx_msg void OnSize(UINT nType, int cx, int cy);
	DECLARE_MESSAGE_MAP()

private:
	void OnDVTimeChanged() override;
	void OnError() override;
	void AttachWindows();

	HWND m_notifyWnd = nullptr;
};
