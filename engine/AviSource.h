// AviSource.h : DV frames from AVI files
#pragma once

#include "DShowBase.h"
#include "InputGraph.h"

// One DV AVI file (type 1 or type 2).
class CAVIReader : public CInputGraph {
public:
	explicit CAVIReader(const std::wstring& filename);
};

// Plays a list of AVI files back to back as one stream. Each file needs its own
// graph, so the next one is built on a helper thread when the current one ends.
class CAVIJoiner : public CFrameSource, public CFrameHandler {
public:
	// filenames: '|'-separated list; each entry may contain wildcards.
	explicit CAVIJoiner(const std::wstring& filenames);
	~CAVIJoiner() override;

	void GetMediaType(CMediaType* type) override;
	void Run(CFrameHandler* handler) override;
	void Stop() override;

	void HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame) override;
	void EndOfStream() override;

private:
	void JoinerThread(std::stop_token stop);

	std::vector<std::wstring> m_filenames;
	std::size_t m_next = 0;
	std::unique_ptr<CAVIReader> m_reader;
	std::atomic<CFrameHandler*> m_handler{nullptr};
	std::atomic<bool> m_stopping{false};

	std::mutex m_mutex;
	std::condition_variable_any m_readerEnded;
	bool m_readerEndedFlag = false;
	std::jthread m_thread;
};
