// DVEngine.cpp : owns the DV pipeline, its state machine and worker thread

#include "DShowBase.h"
#include "DVEngine.h"

#include "AviSource.h"
#include "AviWriter.h"
#include "ComApartment.h"
#include "DShowError.h"
#include "DVDevice.h"
#include "DVTimecode.h"
#include "Monitor.h"

namespace {

constexpr std::size_t kQueueFrames = 100;

} // namespace

DVEngine::DVEngine(DVEngineEvents* events) : m_events(events)
{}

DVEngine::~DVEngine()
{
	Destroy();
}

void DVEngine::ResizePreview()
{
	if (m_monitor)
		m_monitor->Resize();
}

std::size_t DVEngine::GetQueueLoad() const
{
	return m_queue ? m_queue->Load() : 0;
}

std::wstring DVEngine::TakeError()
{
	std::lock_guard lock(m_mutex);
	return std::exchange(m_error, {});
}

void DVEngine::ReportError(const std::wstring& message)
{
	{
		std::lock_guard lock(m_mutex);
		if (!m_error.empty())
			return; // keep the first error; later ones are usually consequences
		m_error = message;
	}
	if (m_events)
		m_events->OnError();
}

void DVEngine::NotifyTimeChange(std::time_t dvTime)
{
	m_dvTime = dvTime;
	if (m_events)
		m_events->OnDVTimeChanged();
}

void DVEngine::Destroy()
{
	m_state = Idle;

	// Wake everything that might be blocked on the queue or a source, then join.
	if (m_queue)
		m_queue->Close();
	if (m_aviJoiner)
		m_aviJoiner->Stop();
	if (m_dvInput)
		m_dvInput->Stop();
	if (m_thread.joinable()) {
		m_thread.request_stop();
		m_thread.join();
	}

	m_aviJoiner.reset();
	if (m_dvInput) {
		if (m_DVctrl)
			m_dvInput->CtrlStop();
		m_dvInput.reset();
	}
	m_aviWriter.reset();
	if (m_dvOutput) {
		if (m_DVctrl)
			m_dvOutput->CtrlStop();
		m_dvOutput.reset();
	}
	m_monitor.reset();
	m_queue.reset();

	m_dropped = 0;
	m_counter = -1;
	m_time = -1;
	m_captureTime = 0;
	m_framesReceived = 0;
}

void DVEngine::HandleFrame(REFERENCE_TIME duration, std::span<const BYTE> frame)
{
	++m_framesReceived;
	try {
		m_queue->Put(duration, frame);
	} catch (const std::length_error&) {
		ReportError(L"Received a DV frame larger than expected");
		m_queue->Close();
	}
}

void DVEngine::EndOfStream()
{
	m_queue->Close();
}

void DVEngine::SourceError(const std::wstring& message)
{
	ReportError(message);
	m_queue->Close();
}

// The preview is optional: without a working DV decoder, capture and record
// still run, just without a picture.
void DVEngine::CreateMonitor(const CMediaType& type)
{
	if (!m_previewWnd)
		return;
	try {
		m_monitor = std::make_unique<CMonitor>(m_previewWnd, type);
	} catch (const DShowError& e) {
		OutputDebugStringW((L"Preview disabled: " + e.Message() + L"\n").c_str());
		m_monitor.reset();
	}
}

void DVEngine::StartWorker(void (DVEngine::*worker)(std::stop_token))
{
	m_thread = std::jthread([this, worker](std::stop_token stop) { (this->*worker)(stop); });
}

void DVEngine::BuildCapturing(const std::wstring& device)
{
	Destroy();
	TakeError();

	m_dvInput = std::make_unique<CDVInput>(device);

	CMediaType type;
	m_dvInput->GetMediaType(&type);

	CreateMonitor(type);
	m_queue = std::make_unique<windv::FrameQueue>(kQueueFrames, type.GetSampleSize());

	m_state = CapturePaused;
	m_dvInput->Run(this);
	StartWorker(&DVEngine::CapturingThread);
}

void DVEngine::StopCapturing()
{
	State expected = Capturing;
	if (m_state.compare_exchange_strong(expected, CapturePaused) && m_DVctrl)
		m_dvInput->CtrlPause();
}

void DVEngine::StartCapturing(const std::wstring& filename, const std::wstring& dtformat, int ndigits,
                              REFERENCE_TIME captureTime)
{
	if (m_state != CapturePaused)
		return;
	{
		std::lock_guard lock(m_mutex);
		m_target = {filename, dtformat, ndigits};
	}
	m_captureTime = captureTime;
	m_state = Capturing;
	if (m_DVctrl)
		m_dvInput->CtrlPlay();
}

void DVEngine::BuildRecording(const std::wstring& filenames, const std::wstring& device)
{
	Destroy();
	TakeError();

	m_aviJoiner = std::make_unique<CAVIJoiner>(filenames);

	CMediaType type;
	m_aviJoiner->GetMediaType(&type);

	CreateMonitor(type);
	m_dvOutput = std::make_unique<CDVOutput>(device, type);
	m_queue = std::make_unique<windv::FrameQueue>(kQueueFrames, type.GetSampleSize());

	m_state = RecordPaused;
	m_aviJoiner->Run(this);
	if (m_DVctrl)
		m_dvOutput->CtrlRecPause();
	StartWorker(&DVEngine::RecordingThread);
}

void DVEngine::StopRecording()
{
	State expected = Recording;
	if (m_state.compare_exchange_strong(expected, RecordPaused) && m_DVctrl)
		m_dvOutput->CtrlRecPause();
}

void DVEngine::StartRecording()
{
	State expected = RecordPaused;
	if (m_state.compare_exchange_strong(expected, Recording) && m_DVctrl)
		m_dvOutput->CtrlRecord();
}

bool DVEngine::CanControlDeck() const
{
	return (m_dvInput && m_dvInput->CanControl()) || (m_dvOutput && m_dvOutput->CanControl());
}

windv::DeckMode DVEngine::GetDeckMode()
{
	if (m_dvInput)
		return m_dvInput->Mode();
	if (m_dvOutput)
		return m_dvOutput->Mode();
	return windv::DeckMode::Unknown;
}

void DVEngine::Transport(windv::DeckCommand command)
{
	if (m_dvInput)
		m_dvInput->Command(command);
}

void DVEngine::CapturingThread(std::stop_token /*stop*/)
{
	ComApartment com;
	try {
		CaptureLoop();
		FinishWriter();
	} catch (const DShowError& e) {
		ReportError(e.Message());
	} catch (const std::exception& e) {
		ReportError(Widen(e.what()));
	}
	m_aviWriter.reset();
	NotifyTimeChange(0);
}

void DVEngine::FinishWriter()
{
	if (auto writer = std::move(m_aviWriter))
		writer->Finish();
}

void DVEngine::CaptureLoop()
{
	CMediaType type;
	m_dvInput->GetMediaType(&type);

	long nFrames = 0;
	long counter = 0;
	long dropped = m_dvInput->GetDroppedFrames();
	std::time_t dvTime = 0, lastValidDVTime = 0;
	m_counter = 0;
	m_time = 0;

	while (m_state != Idle) {
		const auto frame = m_queue->Get();
		if (!frame)
			break;

		// Track the camcorder's recording timestamp; a jump means a new scene.
		const std::time_t newDVTime = windv::GetDVRecordingTime(frame->data).value_or(0);
		std::time_t deltaDVTime = 0;
		if (newDVTime != dvTime) {
			if (newDVTime > 0) {
				if (lastValidDVTime > 0)
					deltaDVTime =
					    newDVTime > lastValidDVTime ? newDVTime - lastValidDVTime : lastValidDVTime - newDVTime;
				lastValidDVTime = newDVTime;
			}
			dvTime = newDVTime;
			NotifyTimeChange(dvTime);
		}

		// Only preview while the queue is draining comfortably.
		if (m_monitor && m_queue->Load() < m_queue->Capacity() / 2)
			m_monitor->HandleFrame(frame->duration, frame->data);

		if (m_state == Capturing) {
			const int threshold = m_discontinuityThreshold;
			if (m_aviWriter && (nFrames >= m_maxAVIFrames || (threshold > 0 && deltaDVTime > threshold)))
				FinishWriter();
			if (!m_aviWriter) {
				CaptureTarget target;
				{
					std::lock_guard lock(m_mutex);
					target = m_target;
				}
				m_aviWriter = std::make_unique<CAVIWriter>(target.filename, target.dtformat, target.ndigits, dvTime,
				                                           m_type2AVI, type);
				nFrames = 0;
				counter = 0;
			}
			if (counter % (std::max)(m_everyNth.load(), 1) == 0) {
				m_aviWriter->HandleFrame(frame->duration, frame->data);
				++nFrames;
			}
			if (dvTime > 0 && m_aviWriter->m_dvTime <= 0)
				m_aviWriter->m_dvTime = dvTime;
			++counter;
			++m_counter;
			m_time += frame->duration;

			const REFERENCE_TIME captureTime = m_captureTime;
			if (captureTime && m_time >= captureTime) {
				m_captureTime = 0;
				State expected = Capturing;
				m_state.compare_exchange_strong(expected, Finished);
			}
			m_dropped = m_dvInput->GetDroppedFrames() - dropped;
		} else {
			FinishWriter();
			dropped = m_dvInput->GetDroppedFrames();
			if (m_state != Finished) {
				m_dropped = 0;
				m_counter = 0;
				m_time = 0;
			}
		}
	}
}

void DVEngine::RecordingThread(std::stop_token /*stop*/)
{
	ComApartment com;
	NotifyTimeChange(0);
	try {
		RecordLoop();
	} catch (const DShowError& e) {
		ReportError(e.Message());
	} catch (const std::exception& e) {
		ReportError(Widen(e.what()));
	}
	NotifyTimeChange(0);
}

void DVEngine::RecordLoop()
{
	auto frame = m_queue->Get();
	if (!frame)
		return;

	m_counter = 0;
	m_time = 0;
	std::time_t dvTime = 0;
	// While paused or finished the current frame is sent again and again, which
	// keeps a still picture on the DV output. The output queue paces the loop.
	while (m_state != Idle) {
		const std::time_t newDVTime = windv::GetDVRecordingTime(frame->data).value_or(0);
		if (newDVTime != dvTime) {
			dvTime = newDVTime;
			NotifyTimeChange(dvTime);
		}
		if (m_monitor && m_recordPreview && (m_queue->IsClosed() || m_queue->Load() > m_queue->Capacity() / 2))
			m_monitor->HandleFrame(frame->duration, frame->data);

		m_dvOutput->HandleFrame(frame->duration, frame->data);

		if (m_state == Recording) {
			++m_counter;
			m_time += frame->duration;
			if (auto next = m_queue->Get()) {
				frame = next;
			} else {
				State expected = Recording;
				m_state.compare_exchange_strong(expected, Finished);
			}
		}
	}
}
