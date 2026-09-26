// Monitor.h : on-screen DV preview
#pragma once

#include "DShowBase.h"
#include "OutputGraph.h"

// On-screen preview. Frames are offered with HandleFrame(); a helper thread
// takes one whenever the renderer is ready, so preview never slows capture.
class CMonitor : public COutputGraph {
public:
	CMonitor(HWND hWnd, const CMediaType& type);
	~CMonitor() override;

	// Fits the video into the owner window (largest centred 4:3 rectangle).
	void Resize();
	void HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame) override;

	// Applies the preview's audio volume/mute; a no-op if no audio renderer
	// connected. Cheap to call every frame: skips the COM call when unchanged.
	void SetVolume(int volumePercent, bool mute);

private:
	void MonitoringThread(std::stop_token stop);

	HWND m_hWnd;
	CComPtr<IVideoWindow> m_VW;
	CComPtr<IBasicAudio> m_BA; // null if no audio renderer connected
	int m_appliedVolume = -1;
	bool m_appliedMute = false;

	std::mutex m_mutex;
	std::condition_variable_any m_filled;
	CComPtr<IMediaSample> m_sample; // empty buffer waiting for a frame
	bool m_sampleFilled = false;
	std::jthread m_thread;
};
