// TransportLogic.cpp : what a VCR-style button press asks the DV deck to do

#include "TransportLogic.h"

namespace windv {

namespace {

// Playing, paused or cueing: the heads are on the tape and there is a picture.
bool IsOnTape(DeckMode mode)
{
	switch (mode) {
	case DeckMode::Playing:
	case DeckMode::Paused:
	case DeckMode::CueForward:
	case DeckMode::CueReverse:
		return true;
	default:
		return false;
	}
}

} // namespace

DeckRequest ResolveDeckCommand(DeckCommand command, DeckMode current)
{
	// Recording to tape is driven by the record pipeline, not these buttons.
	if (current == DeckMode::Recording || current == DeckMode::RecordPaused)
		return command == DeckCommand::Stop ? DeckRequest::Stop : DeckRequest::None;

	switch (command) {
	case DeckCommand::Play:
		return current == DeckMode::Playing ? DeckRequest::None : DeckRequest::Play;
	case DeckCommand::Pause:
		if (current == DeckMode::Paused)
			return DeckRequest::Play; // pause again resumes, as on a VCR
		return IsOnTape(current) ? DeckRequest::Freeze : DeckRequest::PlayThenFreeze;
	case DeckCommand::Stop:
		return current == DeckMode::Stopped ? DeckRequest::None : DeckRequest::Stop;
	case DeckCommand::FastForward:
		return IsOnTape(current) ? DeckRequest::CueForward : DeckRequest::WindForward;
	case DeckCommand::Rewind:
		return IsOnTape(current) ? DeckRequest::CueReverse : DeckRequest::WindReverse;
	}
	return DeckRequest::None;
}

const wchar_t* DeckModeName(DeckMode mode)
{
	switch (mode) {
	case DeckMode::Stopped:
		return L"Stopped";
	case DeckMode::Playing:
		return L"Playing";
	case DeckMode::Paused:
		return L"Paused";
	case DeckMode::FastForward:
		return L"Fast forward";
	case DeckMode::Rewind:
		return L"Rewind";
	case DeckMode::CueForward:
		return L"Cue forward";
	case DeckMode::CueReverse:
		return L"Cue reverse";
	case DeckMode::Recording:
		return L"Recording";
	case DeckMode::RecordPaused:
		return L"Record paused";
	case DeckMode::Unknown:
		break;
	}
	return L"";
}

} // namespace windv
