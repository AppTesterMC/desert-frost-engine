/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine bring-up.
 */

#ifndef ENGINES_DUNE_MUSIC_H
#define ENGINES_DUNE_MUSIC_H

#include "common/array.h"
#include "common/mutex.h"
#include "common/scummsys.h"

namespace OPL {
class OPL;
}

namespace Dune {

/**
 * Player for Cryo's HERAD music (Remi Herbulot's AdLib sequencer), as stored
 * in the AdLib song files of Dune (ARRAKIS.HSQ, WORMINTR.HSQ, ...).
 *
 * A song has up to nine MIDI-like tracks, one per OPL2 voice, and a bank of
 * 40-byte instruments with velocity/aftertouch/slide "macros". The sequencer
 * runs at 200 Hz. The playback logic follows AdPlug's HERAD player by
 * Stas'M (LGPL 2.1 or later), reduced to the version 1 / OPL2 subset that
 * Dune uses.
 */
class Music {
public:
	Music();
	~Music();

	/** Start a decoded (un-HSQ'd) HERAD song; it loops until stop(). */
	bool play(const Common::Array<byte> &data);
	void stop();
	bool isPlaying() const { return _playing; }

private:
	enum {
		kVoices = 9,
		kInstrumentSize = 40,
		kBendCenter = 0x40,
		kMeasureTicks = 96
	};

	enum NoteState {
		kNoteOff,
		kNoteOn,
		kNoteUpdate
	};

	struct Track {
		uint32 start, size; // Event data within _data
		uint32 position;
		uint32 counter;
		uint32 ticks;
	};

	struct Channel {
		byte program;
		byte note;
		bool keyOn;
		byte bend;
		byte slideDuration;
	};

	void onTimer();
	void rewind();
	void processEvents();
	void executeCommand(uint t);
	uint32 readTicks(Track &track);

	const byte *instrument(uint index) const { return _data.data() + _instrumentOffset + index * kInstrumentSize; }
	void noteOn(uint c, byte note, byte velocity);
	void noteOff(uint c, byte note);
	void aftertouch(uint c, byte value);
	void playNote(uint c, byte note, NoteState state);
	void changeProgram(uint c, uint index);
	void macroOutput(uint c, bool carrier, int8 sensitivity, byte level);
	void macroFeedback(uint c, int8 sensitivity, byte level);
	void macroSlide(uint c);

	OPL::OPL *_opl;
	Common::Mutex _mutex;
	bool _playing;

	Common::Array<byte> _data;
	uint32 _instrumentOffset;
	uint _instrumentCount;
	uint16 _loopStart, _loopEnd, _speed;
	int32 _time;
	uint32 _tickPosition, _totalTicks, _loopPosition;
	bool _songEnd;

	Common::Array<Track> _tracks;
	Common::Array<Track> _loopTracks;
	Channel _channels[kVoices];
};

} // namespace Dune

#endif // ENGINES_DUNE_MUSIC_H
