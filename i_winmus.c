// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	i_winmus.c - MUS to MIDI conversion and streaming playback
//	through the Win32 midiStream API, the role of DOOM95's
//	i_winmus.c.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_winmus.c,v 1.0 win95 Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Windows headers first so their typedefs are in place before the
// game headers; mmsystem.h does not conflict with doomtype.h.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_sound.h"
#include "m_argv.h"

// ---------------------------------------------------------------------------
// MUS format constants
// ---------------------------------------------------------------------------

#define MUS_PERCUSSION_CHAN	15
#define MUS_MIDI_CHANS		16

enum
{
    mus_releasekey	= 0x00,
    mus_presskey	= 0x10,
    mus_pitchwheel	= 0x20,
    mus_systemevent	= 0x30,
    mus_changecontrol	= 0x40,
    mus_scoreend	= 0x60,
};

// MUS controller numbers mapped to MIDI controllers.
static const byte	mus2midcontrol[] =
{
    0,			// 0: instrument change
    0,			// 1: bank select
    1,			// 2: modulation
    7,			// 3: volume
    10,			// 4: pan
    11,			// 5: expression
    91,			// 6: reverb depth
    93,			// 7: chorus depth
    64,			// 8: sustain pedal
    67,			// 9: soft pedal
    120,		// 10: all sounds off
    123,		// 11: all notes off
    126,		// 12: mono
    127,		// 13: poly
};
#define NUM_MUS_CONTROLS	((int)(sizeof(mus2midcontrol)/sizeof(mus2midcontrol[0])))

// Time division: 60 ticks per quarter note with a 1 second tempo
// makes one MUS delay unit equal 1/60th of a second, the classic
// conversion used by every MUS player.
#define MIDI_TIME_DIVISION	60

// MUS file header, all little endian 16 bit values.
typedef struct
{
    byte	id[4];		// "MUS"\x1a
    unsigned short	scorelen;
    unsigned short	scorestart;
    unsigned short	channels;
    unsigned short	secondarychannels;
    unsigned short	instrumentcount;
} musheader_t;

// ---------------------------------------------------------------------------
// Song registry
// ---------------------------------------------------------------------------

typedef struct
{
    boolean	used;
    byte*	musdata;
    byte*	track;		// raw MIDI track bytes for streaming
    int		trklen;
    boolean	playing;
    boolean	looping;
    boolean	paused;
} mussong_t;

#define MAX_SONGS 8

static mussong_t	songs[MAX_SONGS];

static mussong_t*	pCurSong = NULL;
static int		iLastMusicVolume = -1;

// ---------------------------------------------------------------------------
// Streaming state
// ---------------------------------------------------------------------------

#define STREAM_BUFFERS		4
#define STREAM_BUFSIZE		4096

static HMIDISTRM	hMidiStream = NULL;
static UINT		uMidiDeviceID = MIDI_MAPPER;
static MIDIHDR		midiHdrs[STREAM_BUFFERS];
static byte		streambufs[STREAM_BUFFERS][STREAM_BUFSIZE];

static int		iStreamPos = 0;		// read position in track data
static boolean		fStreamOpen = false;
static boolean		fEndOfTrack = false;

// ---------------------------------------------------------------------------
// Buffer reader over the MUS score
// ---------------------------------------------------------------------------

typedef struct
{
    byte*	data;
    int		len;
    int		pos;
} musreader_t;

static int ReadByte(musreader_t* r)
{
    if (r->pos >= r->len)
	return -1;
    return r->data[r->pos++];
}

// MUS delays are little endian groups of 7 bit values, high bit marks
// continuation. MIDI wants big endian variable length.
static void WriteVarLen(byte** p, int value)
{
    int	buffer = value & 0x7f;

    while ((value >>= 7) > 0)
    {
	buffer <<= 8;
	buffer |= 0x80 | (value & 0x7f);
    }

    do
    {
	*(*p)++ = (byte)(buffer & 0xff);
	if (buffer & 0x80)
	    buffer >>= 8;
	else
	    break;
    } while (1);
}

// Map MUS channels onto MIDI channels, keeping drums on channel 9.
static int ChannelMap[MUS_MIDI_CHANS];

static void BuildChannelMap(void)
{
    int	i;
    int	chan = 0;

    for (i = 0 ; i < MUS_MIDI_CHANS ; i++)
    {
	if (i == MUS_PERCUSSION_CHAN)
	    ChannelMap[i] = 9;
	else
	    ChannelMap[i] = chan++;
    }
}

// Scale a velocity by the current music volume setting.
static int ScaleVolume(int velocity)
{
    int	vol = snd_MusicVolume;

    if (vol > 15) vol = 15;
    if (vol < 0) vol = 0;

    velocity = velocity * (vol + 1) / 16;
    if (velocity < 1) velocity = 1;
    if (velocity > 127) velocity = 127;
    return velocity;
}

// ---------------------------------------------------------------------------
// MUS -> MIDI conversion. Produces a raw single-track MIDI event stream
// ready for midiStreamOut.
// ---------------------------------------------------------------------------

static boolean ConvertMusToMidi(byte* musdata, byte** outtrack, int* outlen)
{
    musheader_t*	header;
    musreader_t		reader;
    byte*		out;
    byte*		p;
    int			scorebytes;
    int			channel;
    int			done = 0;

    header = (musheader_t*)musdata;
    if (header->id[0] != 'M' || header->id[1] != 'U' ||
	header->id[2] != 'S' || header->id[3] != 0x1a)
	return false;

    BuildChannelMap();

    // The MUS header carries the extent of the score itself.
    scorebytes = header->scorestart + header->scorelen;

    out = (byte*)malloc(scorebytes * 4 + 1024);
    if (!out)
	return false;
    p = out;

    reader.data = musdata;
    reader.len = scorebytes;
    reader.pos = header->scorestart;

    // Track name.
    *p++ = 0x00;			// delta
    *p++ = 0xFF; *p++ = 0x03; *p++ = 4;
    *p++ = 'D'; *p++ = 'O'; *p++ = 'O'; *p++ = 'M';

    // Tempo: 1000000 microseconds per quarter note.
    *p++ = 0x00;
    *p++ = 0xFF; *p++ = 0x51; *p++ = 0x03;
    *p++ = 0x0F; *p++ = 0x42; *p++ = 0x40;

    while (!done)
    {
	int	delay = 0;

	// Read event groups until one carries the "delay follows" bit,
	// accumulating the delay.
	do
	{
	    int	b0, b1, type;
	    int	midichan;

	    b0 = ReadByte(&reader);
	    if (b0 < 0) { done = 1; break; }

	    b1 = ReadByte(&reader);
	    if (b1 < 0) { done = 1; break; }

	    channel = b0 & 0x0f;
	    type = b1 & 0xf0;
	    midichan = ChannelMap[channel] & 0x0f;

	    switch (type)
	    {
	      case mus_releasekey:
	      {
		  int note = ReadByte(&reader) & 0x7f;
		  *p++ = 0x00;		// no delta within the group
		  *p++ = (byte)(0x80 | midichan);
		  *p++ = (byte)note;
		  *p++ = 0x40;
		  break;
	      }

	      case mus_presskey:
	      {
		  int note = ReadByte(&reader) & 0x7f;
		  int vel = 64;
		  if (b1 & 0x80)
		      vel = ReadByte(&reader) & 0x7f;
		  *p++ = 0x00;
		  *p++ = (byte)(0x90 | midichan);
		  *p++ = (byte)note;
		  *p++ = (byte)ScaleVolume(vel);
		  break;
	      }

	      case mus_pitchwheel:
	      {
		  int b2 = ReadByte(&reader);
		  int b3 = ReadByte(&reader);
		  *p++ = 0x00;
		  *p++ = (byte)(0xE0 | midichan);
		  *p++ = (byte)(b2 & 0x7f);
		  *p++ = (byte)(b3 & 0x7f);
		  break;
	      }

	      case mus_systemevent:
	      {
		  int ctrl = ReadByte(&reader) & 0x7f;
		  if (ctrl < NUM_MUS_CONTROLS)
		  {
		      *p++ = 0x00;
		      *p++ = (byte)(0xB0 | midichan);
		      *p++ = mus2midcontrol[ctrl];
		      *p++ = 0;
		  }
		  break;
	      }

	      case mus_changecontrol:
	      {
		  int ctrl = ReadByte(&reader) & 0x7f;
		  int val = ReadByte(&reader) & 0x7f;
		  if (ctrl == 0)
		  {
		      // Instrument change becomes a program change.
		      *p++ = 0x00;
		      *p++ = (byte)(0xC0 | midichan);
		      *p++ = (byte)val;
		  }
		  else if (ctrl < NUM_MUS_CONTROLS)
		  {
		      *p++ = 0x00;
		      *p++ = (byte)(0xB0 | midichan);
		      *p++ = mus2midcontrol[ctrl];
		      *p++ = (byte)val;
		  }
		  break;
	      }

	      case mus_scoreend:
		  done = 1;
		  break;

	      default:
		  // Unknown event type - bail out gracefully.
		  done = 1;
		  break;
	    }

	    if (b0 & 0x80)
	    {
		// Variable length delay follows.
		int	value = 0;
		int	shift = 0;
		int	b;

		do
		{
		    b = ReadByte(&reader);
		    if (b < 0) { done = 1; break; }
		    value |= (b & 0x7f) << shift;
		    shift += 7;
		} while ((b & 0x80) && shift < 28);

		delay = value;
	    }
	} while (!done && delay == 0);

	if (done)
	    break;

	WriteVarLen(&p, delay ? delay : 1);
    }

    // End of track.
    *p++ = 0x00;
    *p++ = 0xFF; *p++ = 0x2F; *p++ = 0x00;

    *outtrack = out;
    *outlen = (int)(p - out);
    return true;
}

// ---------------------------------------------------------------------------
// Streaming playback
// ---------------------------------------------------------------------------

// Size of a MIDI event starting at pos, so buffers can be split cleanly.
static int MidiEventSize(byte* track, int len, int pos)
{
    int	start = pos;
    int	b;

    if (pos >= len)
	return 0;

    // Delta time.
    do
    {
	b = track[pos++];
    } while ((b & 0x80) && pos < len);

    if (pos >= len)
	return pos - start;

    b = track[pos];

    if (b == 0xFF)
    {
	pos++;
	if (pos >= len) return pos - start;
	b = track[pos++];		// meta type
	do
	{
	    if (pos >= len) return pos - start;
	    b = track[pos++];
	} while (b & 0x80);
	pos += b;
    }
    else if (b == 0xF0 || b == 0xF7)
    {
	pos++;
	do
	{
	    if (pos >= len) return pos - start;
	    b = track[pos++];
	} while (b & 0x80);
	pos += b;
    }
    else if (b & 0x80)
    {
	static const int lengths[8] = { 3, 3, 3, 3, 2, 2, 3, 0 };
	pos += lengths[(b >> 4) & 7];
    }
    else
    {
	// Running status is never produced by our converter.
	pos += 2;
    }

    if (pos > len)
	pos = len;
    return pos - start;
}

// Fill one stream buffer with the next run of complete events.
// Returns the number of bytes written, 0 at end of track.
static int FillStreamBuffer(int index)
{
    byte*	buf = streambufs[index];
    int		filled = 0;

    if (fEndOfTrack)
	return 0;

    while (filled < STREAM_BUFSIZE - 16 && !fEndOfTrack)
    {
	int	evsize = MidiEventSize(pCurSong->track, pCurSong->trklen,
				       iStreamPos);

	if (evsize <= 0)
	{
	    fEndOfTrack = true;
	    break;
	}
	if (filled + evsize > STREAM_BUFSIZE)
	    break;

	memcpy(buf + filled, pCurSong->track + iStreamPos, evsize);
	filled += evsize;
	iStreamPos += evsize;
    }

    return filled;
}

// Queue a buffer to the stream. Called from the callback thread too.
static void QueueBuffer(int index)
{
    MIDIHDR*	hdr = &midiHdrs[index];
    int		size;

    size = FillStreamBuffer(index);
    if (size <= 0)
	return;

    hdr->lpData = (char*)streambufs[index];
    hdr->dwBufferLength = size;
    hdr->dwFlags = 0;

    if (midiOutPrepareHeader((HMIDIOUT)hMidiStream, hdr,
			     sizeof(MIDIHDR)) == MMSYSERR_NOERROR)
	midiStreamOut(hMidiStream, hdr, sizeof(MIDIHDR));
}

static void CALLBACK MidiProc(HMIDIOUT hmo, UINT wMsg,
			      DWORD dwInstance, DWORD dwParam1,
			      DWORD dwParam2)
{
    MIDIHDR*	hdr;

    switch (wMsg)
    {
      case MOM_DONE:
	hdr = (MIDIHDR*)dwParam1;
	midiOutUnprepareHeader((HMIDIOUT)hMidiStream, hdr, sizeof(MIDIHDR));

	if (pCurSong && pCurSong->playing)
	{
	    int	index = (int)(hdr - midiHdrs);

	    if (!fEndOfTrack)
	    {
		QueueBuffer(index);
	    }
	    else if (pCurSong->looping)
	    {
		// Rewind and restart the whole stream.
		iStreamPos = 0;
		fEndOfTrack = false;
		QueueBuffer(index);
	    }
	    else
	    {
		midiStreamStop(hMidiStream);
		pCurSong->playing = false;
	    }
	}
	break;

      default:
	break;
    }
}

static boolean OpenStream(void)
{
    MIDIPROPTIMEDIV	prop;
    int			i;

    if (fStreamOpen)
	return true;

    if (midiStreamOpen(&hMidiStream, &uMidiDeviceID, 1,
		       (DWORD)MidiProc, 0, CALLBACK_FUNCTION)
	!= MMSYSERR_NOERROR)
	return false;

    prop.cbStruct = sizeof(prop);
    prop.dwTimeDiv = MIDI_TIME_DIVISION;
    if (midiStreamProperty(hMidiStream, (LPBYTE)&prop,
			   MIDIPROP_SET | MIDIPROP_TIMEDIV)
	!= MMSYSERR_NOERROR)
    {
	midiStreamClose(hMidiStream);
	hMidiStream = NULL;
	return false;
    }

    memset(midiHdrs, 0, sizeof(midiHdrs));
    for (i = 0 ; i < STREAM_BUFFERS ; i++)
	midiHdrs[i].lpData = (char*)streambufs[i];

    fStreamOpen = true;
    return true;
}

static void CloseStream(void)
{
    int	i;

    if (!fStreamOpen)
	return;

    midiStreamStop(hMidiStream);

    for (i = 0 ; i < STREAM_BUFFERS ; i++)
    {
	if (midiHdrs[i].dwFlags & MHDR_PREPARED)
	    midiOutUnprepareHeader((HMIDIOUT)hMidiStream, &midiHdrs[i],
				   sizeof(MIDIHDR));
    }

    midiStreamClose(hMidiStream);
    hMidiStream = NULL;
    fStreamOpen = false;
}

// Start streaming the given song from the beginning.
static void StartStreaming(mussong_t* song, boolean looping)
{
    int	i;

    pCurSong = song;
    iStreamPos = 0;
    fEndOfTrack = false;
    song->looping = looping;
    song->playing = true;
    song->paused = false;

    midiStreamStop(hMidiStream);

    for (i = 0 ; i < STREAM_BUFFERS ; i++)
	QueueBuffer(i);

    midiStreamRestart(hMidiStream);
}

// ---------------------------------------------------------------------------
// Music API, as called by s_sound.c
// ---------------------------------------------------------------------------

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
    if (pCurSong)
	I_StopSong((int)(pCurSong - songs) + 1);
    CloseStream();
}

void I_SetMusicVolume(int volume)
{
    snd_MusicVolume = volume;

    // Velocity scaling happens at conversion time, so a live volume
    // change restarts the current song with rescaled velocities.
    if (pCurSong && pCurSong->playing &&
	iLastMusicVolume != -1 && volume != iLastMusicVolume)
    {
	boolean	looping = pCurSong->looping;
	byte*	track = NULL;
	int	trklen = 0;

	if (ConvertMusToMidi(pCurSong->musdata, &track, &trklen))
	{
	    free(pCurSong->track);
	    pCurSong->track = track;
	    pCurSong->trklen = trklen;
	    StartStreaming(pCurSong, looping);
	}
    }

    iLastMusicVolume = volume;
}

void I_PauseSong(int handle)
{
    if (handle > 0 && handle <= MAX_SONGS && songs[handle - 1].playing)
    {
	songs[handle - 1].paused = true;
	if (fStreamOpen)
	    midiStreamPause(hMidiStream);
    }
}

void I_ResumeSong(int handle)
{
    if (handle > 0 && handle <= MAX_SONGS && songs[handle - 1].playing)
    {
	songs[handle - 1].paused = false;
	if (fStreamOpen)
	    midiStreamRestart(hMidiStream);
    }
}

int I_RegisterSong(void* data)
{
    mussong_t*	song = NULL;
    byte*	track = NULL;
    int		trklen = 0;
    int		i;

    for (i = 0 ; i < MAX_SONGS ; i++)
    {
	if (!songs[i].used)
	{
	    song = &songs[i];
	    break;
	}
    }
    if (!song)
	return 0;

    if (!ConvertMusToMidi((byte*)data, &track, &trklen))
	return 0;

    song->used = true;
    song->musdata = (byte*)data;
    song->track = track;
    song->trklen = trklen;
    song->playing = false;
    song->looping = false;
    song->paused = false;

    return (int)(song - songs) + 1;
}

void I_UnRegisterSong(int handle)
{
    mussong_t*	song;

    if (handle <= 0 || handle > MAX_SONGS)
	return;

    song = &songs[handle - 1];
    if (!song->used)
	return;

    if (pCurSong == song)
    {
	if (fStreamOpen)
	    midiStreamStop(hMidiStream);
	pCurSong = NULL;
    }

    if (song->track)
	free(song->track);
    memset(song, 0, sizeof(*song));
}

void I_PlaySong(int handle, int looping)
{
    mussong_t*	song;

    if (handle <= 0 || handle > MAX_SONGS)
	return;

    song = &songs[handle - 1];
    if (!song->used)
	return;

    if (!OpenStream())
	return;

    StartStreaming(song, looping);
}

void I_StopSong(int handle)
{
    if (fStreamOpen)
	midiStreamStop(hMidiStream);

    if (handle > 0 && handle <= MAX_SONGS)
    {
	songs[handle - 1].playing = false;
	songs[handle - 1].paused = false;
	if (pCurSong == &songs[handle - 1])
	    pCurSong = NULL;
    }
}

int I_QrySongPlaying(int handle)
{
    if (handle <= 0 || handle > MAX_SONGS)
	return false;

    return songs[handle - 1].playing && !songs[handle - 1].paused;
}
