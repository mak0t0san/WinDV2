// DVDevice.cpp : the DV camcorder / deck - enumeration, transport, input and output

#include "DShowBase.h"
#include "DVDevice.h"

#include "DShowError.h"

namespace {

// Shuttle and trick-play modes from the SDK's xprtdefs.h. <streams.h> pulls in
// the older edevdefs.h instead, which lacks them and clashes with xprtdefs.h.
constexpr long kModePlayFastestFwd = ED_BASE + 933L;
constexpr long kModePlayFastestRev = ED_BASE + 935L;

// What the deck reports while cueing is not what it was asked for: a camcorder
// told PLAY_FASTEST_FWD answers e.g. PLAY_FAST_FWD_6. Each range is a family.
constexpr long kModePlaySlowestFwd = ED_BASE + 934L;
constexpr long kModePlaySlowestRev = ED_BASE + 936L;
constexpr long kModeRewFastest = ED_BASE + 938L;
constexpr long kModeRevPlay = ED_BASE + 939L; // x1 reverse play
constexpr long kModePlaySlowFwdFirst = ED_BASE + 1001L, kModePlaySlowFwdLast = ED_BASE + 1006L;
constexpr long kModePlayFastFwdFirst = ED_BASE + 1007L, kModePlayFastFwdLast = ED_BASE + 1012L;
constexpr long kModePlaySlowRevFirst = ED_BASE + 1013L, kModePlaySlowRevLast = ED_BASE + 1018L;
constexpr long kModePlayFastRevFirst = ED_BASE + 1019L, kModePlayFastRevLast = ED_BASE + 1024L;
constexpr long kModeReverseFreeze = ED_BASE + 1025L;
constexpr long kModePlaySlowFwdX = ED_BASE + 1026L;
constexpr long kModePlayFastFwdX = ED_BASE + 1027L;
constexpr long kModePlaySlowRevX = ED_BASE + 1028L;
constexpr long kModePlayFastRevX = ED_BASE + 1029L;

constexpr bool InRange(long mode, long first, long last)
{
	return mode >= first && mode <= last;
}

windv::DeckMode ToDeckMode(long mode)
{
	using windv::DeckMode;
	switch (mode) {
	case ED_MODE_STOP:
		return DeckMode::Stopped;
	case ED_MODE_PLAY:
	case kModePlaySlowestFwd:
	case kModePlaySlowFwdX:
		return DeckMode::Playing; // slow motion still plays forward with a picture
	case ED_MODE_FREEZE:
	case kModeReverseFreeze:
		return DeckMode::Paused;
	case ED_MODE_FF:
		return DeckMode::FastForward;
	case ED_MODE_REW:
	case kModeRewFastest:
		return DeckMode::Rewind;
	case kModePlayFastestFwd:
	case kModePlayFastFwdX:
		return DeckMode::CueForward;
	case kModePlayFastestRev:
	case kModePlaySlowestRev:
	case kModePlayFastRevX:
	case kModePlaySlowRevX:
	case kModeRevPlay:
		return DeckMode::CueReverse;
	case ED_MODE_RECORD:
		return DeckMode::Recording;
	case ED_MODE_RECORD_FREEZE:
		return DeckMode::RecordPaused;
	default:
		break;
	}
	if (InRange(mode, kModePlaySlowFwdFirst, kModePlaySlowFwdLast))
		return DeckMode::Playing;
	if (InRange(mode, kModePlayFastFwdFirst, kModePlayFastFwdLast))
		return DeckMode::CueForward;
	if (InRange(mode, kModePlaySlowRevFirst, kModePlaySlowRevLast) ||
	    InRange(mode, kModePlayFastRevFirst, kModePlayFastRevLast))
		return DeckMode::CueReverse;
	return DeckMode::Unknown;
}

// Calls visit(friendlyName, moniker, bindContext) for every video capture
// device; visit returns false to stop.
template <typename Visitor>
void ForEachVideoDevice(Visitor&& visit)
{
	CComPtr<ICreateDevEnum> devEnum;
	CheckHR(devEnum.CoCreateInstance(CLSID_SystemDeviceEnum), L"Can't create the system device enumerator");

	CComPtr<IEnumMoniker> monikers;
	const HRESULT hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &monikers, 0);
	CheckSucceeded(hr, L"Can't enumerate video devices");
	if (hr != S_OK) // S_FALSE: the category is empty
		return;

	CComPtr<IBindCtx> bindContext;
	CheckHR(CreateBindCtx(0, &bindContext), L"Can't create a bind context");

	CComPtr<IMoniker> moniker;
	while (monikers->Next(1, &moniker, nullptr) == S_OK) {
		CComPtr<IPropertyBag> bag;
		if (SUCCEEDED(moniker->BindToStorage(bindContext, nullptr, IID_PPV_ARGS(&bag)))) {
			CComVariant name;
			if (SUCCEEDED(bag->Read(L"FriendlyName", &name, nullptr)) && name.vt == VT_BSTR) {
				if (!visit(std::wstring(name.bstrVal), moniker.p, bindContext.p))
					return;
			}
		}
		moniker.Release();
	}
}

CComPtr<IBaseFilter> FindVideoDevice(const std::wstring& device)
{
	CComPtr<IBaseFilter> filter;
	ForEachVideoDevice([&](const std::wstring& name, IMoniker* moniker, IBindCtx* bindContext) {
		if (name != device)
			return true;
		CheckHR(moniker->BindToObject(bindContext, nullptr, IID_PPV_ARGS(&filter)), L"Can't open the video device");
		return false;
	});
	if (!filter)
		throw DShowError(L"Video device \"" + device + L"\" not found", S_OK, DShowError::Cause::DeviceNotFound);
	return filter;
}

} // namespace

std::vector<std::wstring> GetVideoDeviceList()
{
	std::vector<std::wstring> list;
	ForEachVideoDevice([&](const std::wstring& name, IMoniker*, IBindCtx*) {
		list.push_back(name);
		return true;
	});
	return list;
}

/////////////////////////////////////////////////////////////////////////////
// DVTransport

void DVTransport::CtrlAttach(IUnknown* device)
{
	m_ET.Release();
	device->QueryInterface(IID_PPV_ARGS(&m_ET)); // optional: not every device has transport control
}

windv::DeckMode DVTransport::Mode()
{
	using windv::DeckMode;
	long mode = 0;
	if (!m_ET || FAILED(m_ET->get_Mode(&mode)))
		return DeckMode::Unknown;
	return ToDeckMode(mode);
}

void DVTransport::Command(windv::DeckCommand command)
{
	using windv::DeckRequest;
	if (!m_ET)
		return;
	switch (windv::ResolveDeckCommand(command, Mode())) {
	case DeckRequest::None:
		break;
	case DeckRequest::Play:
		m_ET->put_Mode(ED_MODE_PLAY);
		break;
	case DeckRequest::Freeze:
		m_ET->put_Mode(ED_MODE_FREEZE);
		break;
	case DeckRequest::PlayThenFreeze:
		m_ET->put_Mode(ED_MODE_PLAY);
		m_ET->put_Mode(ED_MODE_FREEZE);
		break;
	case DeckRequest::Stop:
		m_ET->put_Mode(ED_MODE_STOP);
		break;
	case DeckRequest::WindForward:
		m_ET->put_Mode(ED_MODE_FF);
		break;
	case DeckRequest::WindReverse:
		m_ET->put_Mode(ED_MODE_REW);
		break;
	case DeckRequest::CueForward:
		m_ET->put_Mode(kModePlayFastestFwd);
		break;
	case DeckRequest::CueReverse:
		m_ET->put_Mode(kModePlayFastestRev);
		break;
	}
}

void DVTransport::CtrlStop()
{
	if (m_ET)
		m_ET->put_Mode(ED_MODE_STOP);
}

void DVTransport::CtrlPlay()
{
	if (m_ET)
		m_ET->put_Mode(ED_MODE_PLAY);
}

void DVTransport::CtrlPause()
{
	if (m_ET) {
		m_ET->put_Mode(ED_MODE_PLAY);
		m_ET->put_Mode(ED_MODE_FREEZE);
	}
}

void DVTransport::CtrlRecord()
{
	if (m_ET)
		m_ET->put_Mode(ED_MODE_RECORD);
}

void DVTransport::CtrlRecPause()
{
	if (m_ET)
		m_ET->put_Mode(ED_MODE_RECORD_FREEZE);
}

/////////////////////////////////////////////////////////////////////////////
// CDVInput

CDVInput::CDVInput(const std::wstring& device)
{
	CComPtr<IBaseFilter> source = FindVideoDevice(device);
	CtrlAttach(source);
	CheckHR(m_FG->AddFilter(source, L"DVin"), L"Can't add the DV device to the graph");
	CheckHR(m_GB->RenderStream(nullptr, &MEDIATYPE_Interleaved, source, nullptr, m_inputFilterRef),
	        L"Can't connect to the DV device (is another program using it?)");

	CComPtr<IPin> devicePin;
	CheckHR(InputPin()->ConnectedTo(&devicePin), L"Can't find the DV output pin");
	CheckHR(devicePin.QueryInterface(&m_DF), L"Can't find IAMDroppedFrames");
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
}

long CDVInput::GetDroppedFrames()
{
	long dropped = -1;
	m_DF->GetNumDropped(&dropped);
	return dropped;
}

/////////////////////////////////////////////////////////////////////////////
// CDVOutput

CDVOutput::CDVOutput(const std::wstring& device, const CMediaType& type) : COutputGraph(type, 10)
{
	CComPtr<IBaseFilter> sink = FindVideoDevice(device);
	CtrlAttach(sink);
	CheckHR(m_FG->AddFilter(sink, L"DVout"), L"Can't add the DV device to the graph");
	CheckHR(m_GB->RenderStream(nullptr, nullptr, m_outputFilterRef, nullptr, sink),
	        L"Can't connect to the DV device (is another program using it?)");
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
	if (m_MC->Run() != S_OK) {
		OAFilterState state = State_Stopped;
		CheckHR(m_MC->GetState(1000, &state), L"Can't start DV output");
		if (state != State_Running)
			throw DShowError(L"DV output not running");
	}
}

CDVOutput::~CDVOutput()
{
	DeliverEndOfStream();
	WaitForCompletion();
}
