// DVDevice.h : the DV camcorder / deck - enumeration, transport, input and output
#pragma once

#include "DShowBase.h"
#include "InputGraph.h"
#include "OutputGraph.h"
#include "TransportLogic.h"

// Friendly names of all video capture devices.
std::vector<std::wstring> GetVideoDeviceList();

// Camcorder transport control (play/pause/record) through IAMExtTransport.
class DVTransport {
public:
	void CtrlAttach(IUnknown* device);
	// False if the device has no transport control (e.g. a webcam-style source).
	bool CanControl() const { return m_ET != nullptr; }
	windv::DeckMode Mode();
	// A VCR button; see TransportLogic.h for what each one does.
	void Command(windv::DeckCommand command);

	void CtrlStop();
	void CtrlPlay();
	void CtrlPause();
	void CtrlRecord();
	void CtrlRecPause();

private:
	CComPtr<IAMExtTransport> m_ET;
};

// Frames from the camcorder.
class CDVInput : public CInputGraph, public DVTransport {
public:
	explicit CDVInput(const std::wstring& device);
	long GetDroppedFrames();

private:
	CComPtr<IAMDroppedFrames> m_DF;
};

// Frames to the camcorder (record to tape).
class CDVOutput : public COutputGraph, public DVTransport {
public:
	CDVOutput(const std::wstring& device, const CMediaType& type);
	~CDVOutput() override;
};
