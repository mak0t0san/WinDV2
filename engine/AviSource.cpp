// AviSource.cpp : DV frames from AVI files

#include "DShowBase.h"
#include "AviSource.h"

#include "ComApartment.h"
#include "DShowError.h"
#include "FileFind.h"

namespace {

std::wstring Trim(std::wstring_view text)
{
	const auto first = text.find_first_not_of(L" \t\r\n");
	if (first == std::wstring_view::npos)
		return {};
	const auto last = text.find_last_not_of(L" \t\r\n");
	return std::wstring(text.substr(first, last - first + 1));
}

} // namespace

/////////////////////////////////////////////////////////////////////////////
// CAVIReader

CAVIReader::CAVIReader(const std::wstring& filename)
{
	CComPtr<IBaseFilter> source;
	CheckSucceeded(m_FG->AddSourceFilter(filename.c_str(), L"File source", &source), L"Can't open " + filename);

	CComPtr<IBaseFilter> splitter;
	CheckHR(splitter.CoCreateInstance(CLSID_AviSplitter), L"Can't create the AVI splitter");
	CheckHR(m_FG->AddFilter(splitter, L"AVI Splitter"), L"Can't add the AVI splitter");
	CheckHR(m_GB->RenderStream(nullptr, nullptr, source, nullptr, splitter), filename + L" is not an AVI file");

	// Type-1 AVI: the interleaved DV stream connects directly.
	const HRESULT hr = m_GB->RenderStream(nullptr, &MEDIATYPE_Interleaved, splitter, nullptr, m_inputFilterRef);
	if (hr != S_OK || !IsInputConnected()) {
		// Type-2 AVI: separate video and audio streams go back through the DV muxer.
		CComPtr<IBaseFilter> muxer;
		CheckHR(muxer.CoCreateInstance(CLSID_DVMux), L"Can't create the DV muxer");
		CheckHR(m_FG->AddFilter(muxer, L"DV muxer"), L"Can't add the DV muxer");
		CheckSucceeded(m_GB->RenderStream(nullptr, &MEDIATYPE_Video, splitter, nullptr, muxer),
		               filename + L" has no DV video stream");
		m_GB->RenderStream(nullptr, &MEDIATYPE_Audio, splitter, nullptr, muxer); // audio is optional
		CheckSucceeded(m_GB->RenderStream(nullptr, nullptr, muxer, nullptr, m_inputFilterRef),
		               filename + L" is not a DV AVI file");
	}
	if (!IsInputConnected())
		throw DShowError(filename + L" is not a DV AVI file");
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
}

/////////////////////////////////////////////////////////////////////////////
// CAVIJoiner

CAVIJoiner::CAVIJoiner(const std::wstring& filenames)
{
	std::size_t start = 0;
	while (start <= filenames.size()) {
		std::size_t end = filenames.find(L'|', start);
		if (end == std::wstring::npos)
			end = filenames.size();
		const std::wstring pattern = Trim(std::wstring_view(filenames).substr(start, end - start));
		start = end + 1;
		if (pattern.empty())
			continue;

		const std::vector<FoundFile> found = FindFiles(pattern);
		if (found.empty())
			throw DShowError(pattern + L": file not found");
		std::vector<std::wstring> matches;
		for (const FoundFile& file : found) {
			if (!file.isDirectory)
				matches.push_back(file.path);
		}
		std::ranges::sort(
		    matches, [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
		m_filenames.insert(m_filenames.end(), matches.begin(), matches.end());
	}

	if (m_filenames.empty())
		throw DShowError(L"No file selected");
	m_reader = std::make_unique<CAVIReader>(m_filenames[m_next++]);
}

CAVIJoiner::~CAVIJoiner()
{
	Stop();
}

void CAVIJoiner::GetMediaType(CMediaType* type)
{
	m_reader->GetMediaType(type);
}

void CAVIJoiner::Run(CFrameHandler* handler)
{
	m_handler = handler;
	m_thread = std::jthread([this](std::stop_token stop) { JoinerThread(stop); });
	m_reader->Run(this);
}

void CAVIJoiner::Stop()
{
	m_stopping = true;
	if (m_thread.joinable()) {
		m_thread.request_stop();
		m_thread.join();
	}
	m_reader.reset();
	m_stopping = false;
	m_handler = nullptr;
}

void CAVIJoiner::HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame)
{
	if (m_stopping)
		return;
	if (CFrameHandler* handler = m_handler.load())
		handler->HandleFrame(duration, frame);
}

void CAVIJoiner::EndOfStream()
{
	{
		std::lock_guard lock(m_mutex);
		m_readerEndedFlag = true;
	}
	m_readerEnded.notify_one();
}

void CAVIJoiner::JoinerThread(std::stop_token stop)
{
	ComApartment com;
	try {
		for (;;) {
			{
				std::unique_lock lock(m_mutex);
				if (!m_readerEnded.wait(lock, stop, [this] { return m_readerEndedFlag; }))
					return; // stop requested
				m_readerEndedFlag = false;
			}

			// The finished reader is destroyed here rather than on its own
			// streaming thread, which would deadlock stopping its graph.
			m_reader.reset();
			if (m_next >= m_filenames.size()) {
				if (CFrameHandler* handler = m_handler.load())
					handler->EndOfStream();
				return;
			}
			m_reader = std::make_unique<CAVIReader>(m_filenames[m_next++]);
			m_reader->Run(this);
		}
	} catch (const DShowError& e) {
		if (CFrameHandler* handler = m_handler.load())
			handler->SourceError(e.Message());
	}
}
