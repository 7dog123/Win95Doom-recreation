// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// i_win32.cpp
//
// Win32 system services for the DOOM95 engine: engine ticks bridged
// to Win32 timing, the VirtualAlloc back-end under the zone memory
// allocator, fatal-error and shutdown orchestration, and small
// platform probes.  Source #53 in the original DOOM95.EXE debug
// info; these duties historically lived apart from the video/input
// mass in i_w95gdk.cpp.
//
//-----------------------------------------------------------------------------

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define __BYTEBOOL__

extern "C" {
#include "doomdef.h"
#include "doomstat.h"
#include "d_ticcmd.h"
#include "d_net.h"
#include "i_system.h"
#include "i_video.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
}

// from doom95.cpp - C++ linkage over there
void	MessageError(char* text);

// ---------------------------------------------------------------------------
// CPU capability probe.
//
// The original binary carried a startup symbol named set386 from the
// Watcom era; on Win32 the loader already guarantees a 386, so this
// records the extras beyond it that code may safely exploit.
// ---------------------------------------------------------------------------

static unsigned	cpuFeatures;

#define CPUFCAP_RDTSC	0x00000001
#define CPUFCAP_MMX	0x00000002
#define CPUFCAP_CX8	0x00000004

void set386(void)
{
    if (IsProcessorFeaturePresent(PF_RDTSC_INSTRUCTION_AVAILABLE))
	cpuFeatures |= CPUFCAP_RDTSC;
    if (IsProcessorFeaturePresent(PF_MMX_INSTRUCTIONS_AVAILABLE))
	cpuFeatures |= CPUFCAP_MMX;
    if (IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE_DOUBLE))
	cpuFeatures |= CPUFCAP_CX8;
}

unsigned I_CpuFeatures(void)
{
    return cpuFeatures;
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

static DWORD	dwBaseTime = 0;

int I_GetTime(void)
{
    if (!dwBaseTime)
	dwBaseTime = timeGetTime();
    return (timeGetTime() - dwBaseTime) * TICRATE / 1000;
}

void I_WaitVBL(int count)
{
    Sleep(count * (1000 / 70));
}

// ---------------------------------------------------------------------------
// Zone memory - VirtualAlloc back-end under z_zone.
// ---------------------------------------------------------------------------

byte* I_ZoneBase(int* size)
{
    int		mb = 8;
    int		p;
    byte*	mem;

    p = M_CheckParm("-mb");
    if (p && p < myargc - 1)
	mb = atoi(myargv[p + 1]);
    if (mb < 6)
	mb = 6;

    mem = (byte*)VirtualAlloc(NULL, mb * 1024 * 1024,
			      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem)
	I_Error("Could not allocate %i MB of zone memory", mb);

    printf("zone memory: %i MB allocated\n", mb);
    *size = mb * 1024 * 1024;
    return mem;
}

byte* I_AllocLow(int length)
{
    byte*	mem;

    mem = (byte*)VirtualAlloc(NULL, length,
			      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem)
	I_Error("I_AllocLow: VirtualAlloc failed");
    memset(mem, 0, length);
    return mem;
}

// ---------------------------------------------------------------------------
// Errors and shutdown.
// ---------------------------------------------------------------------------

void I_Error(char* error, ...)
{
    va_list	argptr;
    char	msg[1024];

    va_start(argptr, error);
    vsprintf(msg, error, argptr);
    va_end(argptr);

    // Leave exclusive mode so the message box is visible.
    I_ShutdownGraphics();

    MessageError(msg);
    exit(-1);
}

void I_Shutdown(void)
{
    I_ShutdownGraphics();
}

void I_Quit(void)
{
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults();
    I_ShutdownGraphics();
    exit(0);
}

// ---------------------------------------------------------------------------
// Engine hook stubs.
// ---------------------------------------------------------------------------

void I_Tactile(int on, int off, int total)
{
}

ticcmd_t* I_BaseTiccmd(void)
{
    static ticcmd_t	emptycmd;
    return &emptycmd;
}

// ---------------------------------------------------------------------------
// Startup.
// ---------------------------------------------------------------------------

void I_Init(void)
{
    set386();
    // Timer starts on first I_GetTime; sound comes up here too.
    I_InitSound();
}

// ---------------------------------------------------------------------------
// Paths.
// ---------------------------------------------------------------------------

extern "C" void I_GetExeDir(char* buf, int maxlen)
{
    char	exePath[MAX_PATH];
    char*	lastSlash;

    buf[0] = 0;
    if (GetModuleFileName(NULL, exePath, sizeof(exePath)) == 0)
	return;

    lastSlash = strrchr(exePath, '\\');
    if (!lastSlash)
	return;

    if (lastSlash - exePath >= maxlen)
	return;

    memcpy(buf, exePath, lastSlash - exePath);
    buf[lastSlash - exePath] = 0;
}
