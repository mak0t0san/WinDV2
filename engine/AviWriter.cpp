// AviWriter.cpp : writing captured DV frames to AVI files

#include "DShowBase.h"
#include "AviWriter.h"

#include "CaptureNaming.h"
#include "DShowError.h"
#include "FileFind.h"
#include "TimeFormat.h"

namespace {

// Next free capture filename for base + formatted date (see CaptureNaming.h).
std::wstring NextCaptureFilename(const std::wstring& base, const std::wstring& dtformat, int ndigits, std::time_t tim)
{
	if (tim <= 0)
		tim = std::time(nullptr);
	const std::wstring stem = windv::CaptureStem(base, windv::FormatTime(dtformat, tim));

	std::vector<std::wstring> existing;
	for (const FoundFile& file : FindFiles(windv::CaptureSearchPattern(stem)))
		existing.push_back(file.name);
	return windv::NextCaptureFilename(stem, ndigits, existing);
}

} // namespace

CAVIWriter::CAVIWriter(const std::wstring& base, const std::wstring& dtformat, int ndigits, std::time_t dvTime,
                       bool type2AVI, const CMediaType& type)
    : COutputGraph(type), m_dvTime(dvTime), m_base(base), m_dtformat(dtformat), m_ndigits(ndigits)
{
	m_tmpfile = NextCaptureFilename(m_base, L"~" + m_dtformat, m_ndigits, m_dvTime);

	CComPtr<IBaseFilter> mux;
	CComPtr<IFileSinkFilter> sink;
	CheckHR(m_GB->SetOutputFileName(&MEDIASUBTYPE_Avi, m_tmpfile.c_str(), &mux, &sink), L"Can't create " + m_tmpfile);
	if (CComQIPtr<IFileSinkFilter2> sink2 = sink)
		sink2->SetMode(AM_FILE_OVERWRITE);

	if (type2AVI) {
		CComPtr<IBaseFilter> splitter;
		CheckHR(splitter.CoCreateInstance(CLSID_DVSplitter), L"Can't create the DV splitter");
		CheckHR(m_FG->AddFilter(splitter, L"DV splitter"), L"Can't add the DV splitter");
		CheckHR(m_GB->RenderStream(nullptr, &MEDIATYPE_Interleaved, m_outputFilterRef, nullptr, splitter),
		        L"Can't connect the DV splitter");
		CheckHR(m_GB->RenderStream(nullptr, &MEDIATYPE_Video, splitter, nullptr, mux),
		        L"Can't connect the video stream to the AVI writer");
		CheckHR(m_GB->RenderStream(nullptr, &MEDIATYPE_Audio, splitter, nullptr, mux),
		        L"Can't connect the audio stream to the AVI writer");
	} else {
		CheckHR(m_GB->RenderStream(nullptr, &MEDIATYPE_Interleaved, m_outputFilterRef, nullptr, mux),
		        L"Can't connect the AVI writer");
	}
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
	CheckSucceeded(m_MC->Run(), L"Can't start writing " + m_tmpfile);
}

CAVIWriter::~CAVIWriter()
{
	if (m_finished)
		return;
	try {
		Finish();
	} catch (const DShowError& e) {
		OutputDebugStringW((e.Message() + L"\n").c_str());
	}
}

void CAVIWriter::Finish()
{
	if (m_finished)
		return;
	m_finished = true;

	DeliverEndOfStream();
	WaitForCompletion();
	m_MC->Stop();

	const std::wstring filename = NextCaptureFilename(m_base, m_dtformat, m_ndigits, m_dvTime);
	if (!MoveFileExW(m_tmpfile.c_str(), filename.c_str(), 0))
		throw DShowError(L"Can't rename " + m_tmpfile + L" to " + filename, HRESULT_FROM_WIN32(GetLastError()));
}
