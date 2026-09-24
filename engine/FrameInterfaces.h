// FrameInterfaces.h : DV frame producers and consumers
//
// A CFrameSource pushes raw DV frames into a CFrameHandler. Sources and sinks
// are small DirectShow graphs built around custom filters (CInputGraph wraps an
// input pin, COutputGraph an output pin), so the engine sees every frame itself.
#pragma once

#include "DShowBase.h"

class CFrameHandler {
public:
	virtual ~CFrameHandler() = default;
	virtual void HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame) = 0;
	// The source has no more frames.
	virtual void EndOfStream() {}
	// The source failed on one of its own threads and has stopped.
	virtual void SourceError(const std::wstring& /*message*/) {}
};

class CFrameSource {
public:
	virtual ~CFrameSource() = default;
	virtual void GetMediaType(CMediaType* type) = 0;
	virtual void Run(CFrameHandler* handler) = 0;
	virtual void Stop() = 0;
};
