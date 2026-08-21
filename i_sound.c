// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	i_sound.c - Sound effects via DirectSound.
//	The software mixing engine is the original one from the
//	Linux release; only the output device layer was replaced,
//	like DOOM95 did.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_sound.c,v 1.0 win95 Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Windows headers first: rpcndr.h declares byte/boolean, so
// doomtype.h must not redeclare them inside this file.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#define __BYTEBOOL__

// With doomtype.h typedefs suppressed, provide the C constants.
#ifndef true
#define true	1
#define false	0
#endif

#include "doomdef.h"
#include "doomstat.h"
#include "sounds.h"
#include "i_sound.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

// The number of internal mixing channels,
//  the samples calculated for each mixing step,
//  and the size of the 16bit stereo mixing buffer.

#define SAMPLECOUNT		512
#define NUM_CHANNELS		8
// It is 2 for 16bit, and 2 for two channels.
#define BUFMUL                  4
#define MIXBUFFERSIZE		(SAMPLECOUNT*BUFMUL)

#define SAMPLERATE		11025	// Hz

// The ring buffer holds this many mixing chunks.
#define DS_NUMCHUNKS		8
// Keep the write cursor this many chunks ahead of play.
#define DS_LEADCHUNKS		3

// The actual lengths of all sound effects.
int 		lengths[NUMSFX];

// The global mixing buffer.
signed short	mixbuffer[MIXBUFFERSIZE];

// The channel step amount...
unsigned int	channelstep[NUM_CHANNELS];
// ... and a 0.16 bit remainder of last step.
unsigned int	channelstepremainder[NUM_CHANNELS];

// The channel data pointers, start and end.
unsigned char*	channels[NUM_CHANNELS];
unsigned char*	channelsend[NUM_CHANNELS];

// Time/gametic that the channel started playing,
int		channelstart[NUM_CHANNELS];

// The sound in channel handles.
int 		channelhandles[NUM_CHANNELS];

// SFX id of the playing sound effect.
int		channelids[NUM_CHANNELS];

// Pitch to stepping lookup.
int		steptable[256];

// Volume lookups.
int		vol_lookup[128*256];

// Hardware left and right channel volume lookup.
int*		channelleftvol_lookup[NUM_CHANNELS];
int*		channelrightvol_lookup[NUM_CHANNELS];

// ---------------------------------------------------------------------------
// DirectSound device
// ---------------------------------------------------------------------------

static LPDIRECTSOUND		lpDS = NULL;
static LPDIRECTSOUNDBUFFER	lpDSPrimary = NULL;
static LPDIRECTSOUNDBUFFER	lpDSBuffer = NULL;

static DWORD	dwBufferSize = 0;
static DWORD	dwChunkBytes = SAMPLECOUNT * BUFMUL;
static DWORD	dwWritePos = 0;		// our logical write cursor
static boolean	fSoundStarted = false;

// ---------------------------------------------------------------------------
// Loading of sound lumps (unchanged from the Linux version).
// ---------------------------------------------------------------------------

void* getsfx(char* sfxname, int* len)
{
    unsigned char*      sfx;
    unsigned char*      paddedsfx;
    int                 i;
    int                 size;
    int                 paddedsize;
    char                name[20];
    int                 sfxlump;

    sprintf(name, "ds%s", sfxname);

    // There is a severe problem with the sound handling,
    //  in it is not (yet/anymore) gamemode aware. Use a
    //  default sound for replacement, like the original.
    if (W_CheckNumForName(name) == -1)
	sfxlump = W_GetNumForName("dspistol");
    else
	sfxlump = W_GetNumForName(name);

    size = W_LumpLength(sfxlump);

    sfx = (unsigned char*)W_CacheLumpNum(sfxlump, PU_STATIC);

    // Pads the sound effect out to the mixing buffer size.
    paddedsize = ((size - 8 + (SAMPLECOUNT - 1)) / SAMPLECOUNT) * SAMPLECOUNT;

    paddedsfx = (unsigned char*)Z_Malloc(paddedsize + 8, PU_STATIC, 0);

    memcpy(paddedsfx, sfx, size);
    for (i = size ; i < paddedsize + 8 ; i++)
	paddedsfx[i] = 128;

    Z_Free(sfx);

    *len = paddedsize;
    return (void*)(paddedsfx + 8);
}

// ---------------------------------------------------------------------------
// Channel allocation (unchanged from the Linux version).
// ---------------------------------------------------------------------------

int addsfx(int sfxid, int volume, int step, int seperation)
{
    static unsigned short	handlenums = 0;

    int		i;
    int		rc = -1;

    int		oldest = gametic;
    int		oldestnum = 0;
    int		slot;

    int		rightvol;
    int		leftvol;

    // Chainsaw troubles. Play these one at a time.
    if (sfxid == sfx_sawup
	|| sfxid == sfx_sawidl
	|| sfxid == sfx_sawful
	|| sfxid == sfx_sawhit
	|| sfxid == sfx_stnmov
	|| sfxid == sfx_pistol)
    {
	for (i = 0 ; i < NUM_CHANNELS ; i++)
	{
	    if ((channels[i]) && (channelids[i] == sfxid))
	    {
		channels[i] = 0;
		break;
	    }
	}
    }

    // Find the oldest channel.
    for (i = 0 ; (i < NUM_CHANNELS) && (channels[i]) ; i++)
    {
	if (channelstart[i] < oldest)
	{
	    oldestnum = i;
	    oldest = channelstart[i];
	}
    }

    if (i == NUM_CHANNELS)
	slot = oldestnum;
    else
	slot = i;

    channels[slot] = (unsigned char*)S_sfx[sfxid].data;
    channelsend[slot] = channels[slot] + lengths[sfxid];

    if (!handlenums)
	handlenums = 100;

    channelhandles[slot] = rc = handlenums++;

    channelstep[slot] = step;
    channelstepremainder[slot] = 0;
    channelstart[slot] = gametic;

    // Separation, that is, orientation/stereo. Range is 1 - 256.
    seperation += 1;

    leftvol =
	volume - ((volume * seperation * seperation) >> 16);
    seperation = seperation - 257;
    rightvol =
	volume - ((volume * seperation * seperation) >> 16);

    if (rightvol < 0 || rightvol > 127)
	I_Error("rightvol out of bounds");

    if (leftvol < 0 || leftvol > 127)
	I_Error("leftvol out of bounds");

    channelleftvol_lookup[slot] = &vol_lookup[leftvol * 256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol * 256];

    channelids[slot] = sfxid;

    return rc;
}

// Reapply volume/separation to a playing channel.
static void setchannelparams(int slot, int vol, int sep)
{
    int		rightvol;
    int		leftvol;

    sep += 1;

    leftvol = vol - ((vol * sep * sep) >> 16);
    sep = sep - 257;
    rightvol = vol - ((vol * sep * sep) >> 16);

    if (leftvol < 0)   leftvol = 0;
    if (leftvol > 127) leftvol = 127;
    if (rightvol < 0)  rightvol = 0;
    if (rightvol > 127) rightvol = 127;

    channelleftvol_lookup[slot] = &vol_lookup[leftvol * 256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol * 256];
}

// ---------------------------------------------------------------------------
// SFX API
// ---------------------------------------------------------------------------

void I_SetChannels()
{
    int		i;
    int		j;

    int*	steptablemid = steptable + 128;

    for (i = -128 ; i < 128 ; i++)
	steptablemid[i] = (int)(pow(2.0, (i / 64.0)) * 65536.0);

    // Volume lookup tables, also turning unsigned samples signed.
    for (i = 0 ; i < 128 ; i++)
	for (j = 0 ; j < 256 ; j++)
	    vol_lookup[i * 256 + j] = (i * (j - 128) * 256) / 127;
}

void I_SetSfxVolume(int volume)
{
    snd_SfxVolume = volume;
}

int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    priority = 0;

    id = addsfx(id, vol, steptable[pitch], sep);

    return id;
}

void I_StopSound(int handle)
{
    int		i;

    for (i = 0 ; i < NUM_CHANNELS ; i++)
    {
	if (channelhandles[i] == handle)
	{
	    channels[i] = 0;
	    break;
	}
    }
}

int I_SoundIsPlaying(int handle)
{
    return gametic < handle;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    int		i;

    for (i = 0 ; i < NUM_CHANNELS ; i++)
    {
	if (channels[i] && channelhandles[i] == handle)
	{
	    setchannelparams(i, vol, sep);
	    return;
	}
    }
}

// ---------------------------------------------------------------------------
// The mixer - unchanged from the Linux version.
// ---------------------------------------------------------------------------

void I_UpdateSound(void)
{
    register unsigned int	sample;
    register int		dl;
    register int		dr;

    signed short*		leftout;
    signed short*		rightout;
    signed short*		leftend;
    int				step;
    int				chan;

    leftout = mixbuffer;
    rightout = mixbuffer + 1;
    step = 2;

    leftend = mixbuffer + SAMPLECOUNT * step;

    while (leftout != leftend)
    {
	dl = 0;
	dr = 0;

	for (chan = 0 ; chan < NUM_CHANNELS ; chan++)
	{
	    if (channels[chan])
	    {
		sample = *channels[chan];
		dl += channelleftvol_lookup[chan][sample];
		dr += channelrightvol_lookup[chan][sample];
		channelstepremainder[chan] += channelstep[chan];
		channels[chan] += channelstepremainder[chan] >> 16;
		channelstepremainder[chan] &= 65536 - 1;

		if (channels[chan] >= channelsend[chan])
		    channels[chan] = 0;
	    }
	}

	if (dl > 0x7fff)
	    *leftout = 0x7fff;
	else if (dl < -0x8000)
	    *leftout = -0x8000;
	else
	    *leftout = dl;

	if (dr > 0x7fff)
	    *rightout = 0x7fff;
	else if (dr < -0x8000)
	    *rightout = -0x8000;
	else
	    *rightout = dr;

	leftout += step;
	rightout += step;
    }
}

// ---------------------------------------------------------------------------
// Submit: feed mixed chunks into the DirectSound ring buffer, keeping the
// write cursor a fixed lead ahead of the play cursor.
// ---------------------------------------------------------------------------

static void WriteChunk(void)
{
    HRESULT		hr;
    LPVOID		p1, p2;
    DWORD		n1, n2;

    hr = lpDSBuffer->lpVtbl->Lock(lpDSBuffer, dwWritePos, dwChunkBytes,
				 &p1, &n1, &p2, &n2, 0);
    if (hr == DSERR_BUFFERLOST)
    {
	lpDSBuffer->lpVtbl->Restore(lpDSBuffer);
	hr = lpDSBuffer->lpVtbl->Lock(lpDSBuffer, dwWritePos, dwChunkBytes,
				     &p1, &n1, &p2, &n2, 0);
    }
    if (hr != DS_OK)
	return;

    if (p1) memcpy(p1, mixbuffer, n1);
    if (p2) memcpy(p2, (byte*)mixbuffer + n1, n2);

    lpDSBuffer->lpVtbl->Unlock(lpDSBuffer, p1, n1, p2, n2);

    dwWritePos = (dwWritePos + dwChunkBytes) % dwBufferSize;
}

void I_SubmitSound(void)
{
    DWORD	play, write;
    int		ahead;

    if (!fSoundStarted || !lpDSBuffer)
	return;

    if (lpDSBuffer->lpVtbl->GetCurrentPosition(lpDSBuffer, &play, &write) != DS_OK)
	return;

    ahead = (int)dwWritePos - (int)play;
    if (ahead < 0)
	ahead += dwBufferSize;

    while (ahead < (int)(DS_LEADCHUNKS * dwChunkBytes))
    {
	I_UpdateSound();
	WriteChunk();
	ahead += dwChunkBytes;
    }
}

// ---------------------------------------------------------------------------
// Startup / shutdown
// ---------------------------------------------------------------------------

void I_ShutdownSound(void)
{
    if (!fSoundStarted)
	return;

    if (lpDSBuffer)
    {
	lpDSBuffer->lpVtbl->Stop(lpDSBuffer);
	lpDSBuffer->lpVtbl->Release(lpDSBuffer);
	lpDSBuffer = NULL;
    }
    if (lpDSPrimary)
    {
	lpDSPrimary->lpVtbl->Release(lpDSPrimary);
	lpDSPrimary = NULL;
    }
    if (lpDS)
    {
	lpDS->lpVtbl->Release(lpDS);
	lpDS = NULL;
    }

    fSoundStarted = false;
}

void I_StartupSound(void)
{
    DSBUFFERDESC		dsbd;
    WAVEFORMATEX		wfx;
    int				i;

    if (M_CheckParm("-nosound"))
	return;

    if (DirectSoundCreate(NULL, &lpDS, NULL) != DS_OK)
    {
	fprintf(stderr, "I_InitSound: could not create DirectSound\n");
	return;
    }

    if (lpDS->lpVtbl->SetCooperativeLevel(lpDS, GetActiveWindow(), DSSCL_NORMAL) != DS_OK)
    {
	fprintf(stderr, "I_InitSound: SetCooperativeLevel failed\n");
	I_ShutdownSound();
	return;
    }

    // Primary buffer at our native mixing rate.
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = SAMPLERATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    memset(&dsbd, 0, sizeof(dsbd));
    dsbd.dwSize = sizeof(dsbd);
    dsbd.dwFlags = DSBCAPS_PRIMARYBUFFER;
    if (lpDS->lpVtbl->CreateSoundBuffer(lpDS, &dsbd, &lpDSPrimary, NULL) != DS_OK)
    {
	fprintf(stderr, "I_InitSound: primary buffer failed\n");
	I_ShutdownSound();
	return;
    }
    lpDSPrimary->lpVtbl->SetFormat(lpDSPrimary, &wfx);

    // Secondary looping ring buffer.
    memset(&dsbd, 0, sizeof(dsbd));
    dsbd.dwSize = sizeof(dsbd);
    dsbd.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS |
		   DSBCAPS_CTRLVOLUME;
    dsbd.dwBufferBytes = DS_NUMCHUNKS * dwChunkBytes;
    dsbd.lpwfxFormat = &wfx;

    if (lpDS->lpVtbl->CreateSoundBuffer(lpDS, &dsbd, &lpDSBuffer, NULL) != DS_OK)
    {
	fprintf(stderr, "I_InitSound: secondary buffer failed\n");
	I_ShutdownSound();
	return;
    }

    dwBufferSize = DS_NUMCHUNKS * dwChunkBytes;
    dwWritePos = 0;

    // Pre-fill with silence and start streaming.
    LPVOID	p1, p2;
    DWORD	n1, n2;
    if (lpDSBuffer->lpVtbl->Lock(lpDSBuffer, 0, 0, &p1, &n1, &p2, &n2,
				 DSBLOCK_ENTIREBUFFER) == DS_OK)
    {
	memset(p1, 0, n1);
	if (p2) memset(p2, 0, n2);
	lpDSBuffer->lpVtbl->Unlock(lpDSBuffer, p1, n1, p2, n2);
    }
    lpDSBuffer->lpVtbl->Play(lpDSBuffer, 0, 0, DSBPLAY_LOOPING);

    fSoundStarted = true;

    // Initialize external data (all sounds) at start.
    for (i = 1 ; i < NUMSFX ; i++)
    {
	if (!S_sfx[i].link)
	{
	    S_sfx[i].data = getsfx(S_sfx[i].name, &lengths[i]);
	}
	else
	{
	    S_sfx[i].data = S_sfx[i].link->data;
	    lengths[i] = lengths[(S_sfx[i].link - S_sfx)];
	}
    }

    printf("I_InitSound: DirectSound module ready\n");
    return;
}

// Wrapper matching the classic API used by I_Init.
void I_InitSound(void)
{
    I_StartupSound();
}
