#include <doctest.h>

#include "TransportLogic.h"

#include <string>

using namespace windv;

TEST_CASE("Fast-forward and rewind wind a stopped tape")
{
	CHECK(ResolveDeckCommand(DeckCommand::FastForward, DeckMode::Stopped) == DeckRequest::WindForward);
	CHECK(ResolveDeckCommand(DeckCommand::Rewind, DeckMode::Stopped) == DeckRequest::WindReverse);
	CHECK(ResolveDeckCommand(DeckCommand::FastForward, DeckMode::Unknown) == DeckRequest::WindForward);
	// Switching direction while winding keeps winding.
	CHECK(ResolveDeckCommand(DeckCommand::Rewind, DeckMode::FastForward) == DeckRequest::WindReverse);
	CHECK(ResolveDeckCommand(DeckCommand::FastForward, DeckMode::Rewind) == DeckRequest::WindForward);
}

TEST_CASE("Fast-forward and rewind cue a playing or paused tape")
{
	CHECK(ResolveDeckCommand(DeckCommand::FastForward, DeckMode::Playing) == DeckRequest::CueForward);
	CHECK(ResolveDeckCommand(DeckCommand::Rewind, DeckMode::Playing) == DeckRequest::CueReverse);
	CHECK(ResolveDeckCommand(DeckCommand::FastForward, DeckMode::Paused) == DeckRequest::CueForward);
	CHECK(ResolveDeckCommand(DeckCommand::Rewind, DeckMode::CueForward) == DeckRequest::CueReverse);
}

TEST_CASE("Play, pause and stop")
{
	CHECK(ResolveDeckCommand(DeckCommand::Play, DeckMode::Stopped) == DeckRequest::Play);
	CHECK(ResolveDeckCommand(DeckCommand::Play, DeckMode::CueForward) == DeckRequest::Play);
	CHECK(ResolveDeckCommand(DeckCommand::Play, DeckMode::Playing) == DeckRequest::None);

	CHECK(ResolveDeckCommand(DeckCommand::Pause, DeckMode::Playing) == DeckRequest::Freeze);
	CHECK(ResolveDeckCommand(DeckCommand::Pause, DeckMode::CueReverse) == DeckRequest::Freeze);
	CHECK(ResolveDeckCommand(DeckCommand::Pause, DeckMode::Stopped) == DeckRequest::PlayThenFreeze);
	CHECK(ResolveDeckCommand(DeckCommand::Pause, DeckMode::Paused) == DeckRequest::Play);

	CHECK(ResolveDeckCommand(DeckCommand::Stop, DeckMode::Playing) == DeckRequest::Stop);
	CHECK(ResolveDeckCommand(DeckCommand::Stop, DeckMode::Rewind) == DeckRequest::Stop);
	CHECK(ResolveDeckCommand(DeckCommand::Stop, DeckMode::Stopped) == DeckRequest::None);
}

TEST_CASE("Only stop works while recording to tape")
{
	CHECK(ResolveDeckCommand(DeckCommand::FastForward, DeckMode::Recording) == DeckRequest::None);
	CHECK(ResolveDeckCommand(DeckCommand::Play, DeckMode::RecordPaused) == DeckRequest::None);
	CHECK(ResolveDeckCommand(DeckCommand::Stop, DeckMode::Recording) == DeckRequest::Stop);
}

TEST_CASE("Deck modes have names")
{
	CHECK(std::wstring(DeckModeName(DeckMode::Playing)) == L"Playing");
	CHECK(std::wstring(DeckModeName(DeckMode::Unknown)).empty());
}
