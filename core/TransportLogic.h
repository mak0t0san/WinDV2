// TransportLogic.h : what a VCR-style button press asks the DV deck to do
//
// The buttons behave like a VCR: fast-forward and rewind wind the tape when it
// is stopped, and cue (search with picture) when it is playing or paused.
#pragma once

namespace windv {

// A transport button.
enum class DeckCommand { Play, Pause, Stop, FastForward, Rewind };

// What the deck reports it is doing.
enum class DeckMode {
	Unknown,
	Stopped,
	Playing,
	Paused,
	FastForward, // winding, no picture
	Rewind,      // winding, no picture
	CueForward,  // fast play with picture
	CueReverse,  // fast reverse play with picture
	Recording,
	RecordPaused,
};

// The mode change to send to the deck (IAMExtTransport::put_Mode).
enum class DeckRequest {
	None,           // nothing to do
	Play,           // ED_MODE_PLAY
	Freeze,         // ED_MODE_FREEZE
	PlayThenFreeze, // ED_MODE_PLAY, then ED_MODE_FREEZE: pausing needs a moving tape
	Stop,           // ED_MODE_STOP
	WindForward,    // ED_MODE_FF
	WindReverse,    // ED_MODE_REW
	CueForward,     // ED_MODE_PLAY_FASTEST_FWD
	CueReverse,     // ED_MODE_PLAY_FASTEST_REV
};

DeckRequest ResolveDeckCommand(DeckCommand command, DeckMode current);

// A short user-facing name for the mode ("Playing", "Stopped", ...).
const wchar_t* DeckModeName(DeckMode mode);

} // namespace windv
