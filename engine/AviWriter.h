// AviWriter.h : writing captured DV frames to AVI files
#pragma once

#include "DShowBase.h"
#include "OutputGraph.h"

// Writes one AVI file. The file is written under a temporary "~" name and moved
// to its final name in Finish(), once the DV timestamp for the name is known.
class CAVIWriter : public COutputGraph {
public:
	CAVIWriter(const std::wstring& base, const std::wstring& dtformat, int ndigits, std::time_t dvTime, bool type2AVI,
	           const CMediaType& type);
	~CAVIWriter() override;

	// Flushes the file and renames it. Throws DShowError if the rename fails.
	void Finish();

	std::time_t m_dvTime; // used for the final name; 0 means "now"

private:
	std::wstring m_tmpfile, m_base, m_dtformat;
	int m_ndigits;
	bool m_finished = false;
};
