// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	i_w95gdk.cpp - the Windows 95 "Game SDK" layer.
//	Implements the I_* system, video, input and network interfaces
//	on top of Win32 and DirectDraw, following the module layout of
//	the original DOOM95.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_w95gdk.cpp,v 1.0 win95 Exp $";

// Windows headers first, so rpcndr.h's byte/boolean typedefs
// win over doomtype.h inside this C++ file.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <mmsystem.h>

#define __BYTEBOOL__

extern "C" {
#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_ticcmd.h"
#include "i_system.h"
#include "i_video.h"
#include "i_sound.h"
#include "i_net.h"
#include "d_main.h"
#include "m_argv.h"
#include "m_misc.h"
#include "v_video.h"
#include "i_launch.h"
#include "i_dinput.h"
}

// from doom95.cpp
extern "C" HWND		hwndMain;
extern "C" boolean	fAppActive;
extern "C" boolean	fInFullScreen;
extern "C" boolean	fMouseGrabbed;
extern "C" boolean	fStretchOut;
extern "C" boolean	fPartialPalette;
extern "C" int		iBackWidth;
extern "C" int		iBackHeight;
extern "C" LPDIRECTDRAWSURFACE	lpDDSPrimary;
extern "C" LPDIRECTDRAWSURFACE	lpDDSBack;
extern "C" LPDIRECTDRAWSURFACE	lpDDSOff;
extern "C" int		iDestBPP;
extern "C" int		iRShift, iGShift, iBShift;
extern "C" int		iRMax, iGMax, iBMax;
extern "C" byte		gamePalette[768];
extern void		SetDDPalette(byte* palette);
extern void		PumpMessages(void);
extern void		ToggleFullScreen(void);
extern void		MessageError(char* text);
extern "C" void		finiObjects(void);

void			ComputeStretchRect(int clientw, int clienth);
static boolean		graphics_started = false;

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
// Zone memory
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
// Errors and shutdown
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

void I_Tactile(int on, int off, int total)
{
}

ticcmd_t* I_BaseTiccmd(void)
{
    static ticcmd_t	emptycmd;
    return &emptycmd;
}

void I_Init(void)
{
    // Timer starts on first I_GetTime; sound comes up elsewhere.
    I_InitSound();
}

// ---------------------------------------------------------------------------
// Input event queues.
// Keyboard events are posted from the window procedure, mouse movement is
// polled with cursor recentering, exactly like DOOM95's fFastMouse path.
// ---------------------------------------------------------------------------

typedef struct
{
    int	type;
    int	key;
} keyevent_t;

#define KEYQUEUESIZE	128

static keyevent_t	keyboardque[KEYQUEUESIZE];
static int		kbdhead = 0;
static int		kbdtail = 0;

static int		mousebuttons = 0;
static int		mousex = 0;
static int		mousey = 0;

extern "C" void I_PostKeyboardEvent(int type, int key)
{
    keyevent_t*	ev;

    ev = &keyboardque[kbdhead & (KEYQUEUESIZE - 1)];
    ev->type = type;
    ev->key = key;
    kbdhead++;
}

extern "C" void I_PostMouseButtonEvent(int button, boolean down)
{
    if (down)
	mousebuttons |= (1 << button);
    else
	mousebuttons &= ~(1 << button);
}

static void GrabOrReleaseMouse(void)
{
    RECT	rc;

    if (fAppActive && graphics_started)
    {
	GetClientRect(hwndMain, &rc);
	ClientToScreen(hwndMain, (POINT*)&rc.left);
	ClientToScreen(hwndMain, (POINT*)&rc.right);
	ClipCursor(&rc);
	fMouseGrabbed = true;
    }
    else
    {
	ClipCursor(NULL);
	fMouseGrabbed = false;
    }
}

extern "C" boolean I_IsFullscreen(void)
{
    return fInFullScreen;
}

extern "C" void I_SetAppActive(boolean active)
{
    fAppActive = active;
    GrabOrReleaseMouse();
}

extern "C" void I_NewWindowSize(int width, int height)
{
    // Recompute the letterbox rectangle for the new client size.
    ComputeStretchRect(width, height);
    GrabOrReleaseMouse();
}

// Aspect correct destination rectangle inside a client area.
RECT	rcStretch;

void ComputeStretchRect(int clientw, int clienth)
{
    int	w = clientw;
    int	h = clientw * SCREENHEIGHT / SCREENWIDTH;

    if (h > clienth)
    {
	h = clienth;
	w = clienth * SCREENWIDTH / SCREENHEIGHT;
    }

    rcStretch.left = (clientw - w) / 2;
    rcStretch.top = (clienth - h) / 2;
    rcStretch.right = rcStretch.left + w;
    rcStretch.bottom = rcStretch.top + h;
}

// ---------------------------------------------------------------------------
// Per-tic input processing: drain the keyboard queue and post the mouse.
// ---------------------------------------------------------------------------

void I_StartTic(void)
{
    event_t	event;

    while (kbdtail < kbdhead)
    {
	keyevent_t*	ev = &keyboardque[kbdtail & (KEYQUEUESIZE - 1)];

	event.type = (evtype_t)ev->type;
	event.data1 = ev->key;
	event.data2 = event.data3 = 0;
	D_PostEvent(&event);
	kbdtail++;
    }

    if (fMouseGrabbed)
    {
	POINT	pt;
	RECT	rc;
	int	cx, cy;

	GetClientRect(hwndMain, &rc);
	cx = (rc.left + rc.right) / 2;
	cy = (rc.top + rc.bottom) / 2;

	GetCursorPos(&pt);
	ScreenToClient(hwndMain, &pt);

	if (pt.x != cx || pt.y != cy)
	{
	    mousex += pt.x - cx;
	    mousey += pt.y - cy;

	    pt.x = cx;
	    pt.y = cy;
	    ClientToScreen(hwndMain, &pt);
	    SetCursorPos(pt.x, pt.y);
	}
    }

    if (mousex || mousey || mousebuttons)
    {
	event.type = ev_mouse;
	event.data1 = mousebuttons;
	event.data2 = mousex * 4;
	event.data3 = -mousey * 4;
	D_PostEvent(&event);
	mousex = mousey = 0;
    }

    // DirectInput joystick, like DOOM95's I_ReadDirectInput
    I_ReadDirectInput();
}

void I_StartFrame(void)
{
    PumpMessages();
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

static byte	currentpalette[768];

void I_InitGraphics(void)
{
    // the game display is taking over - drop the splash
    LaunchDone();

    // DirectInput joystick, like DOOM95
    I_StartupJoystick();

    graphics_started = true;
    GrabOrReleaseMouse();
}

void I_ShutdownGraphics(void)
{
    if (!graphics_started)
	return;

    ClipCursor(NULL);
    fMouseGrabbed = false;
    ShowCursor(TRUE);

    I_ShutdownDirectInput();
    finiObjects();
    graphics_started = false;
}

void I_SetPalette(byte* palette)
{
    memcpy(currentpalette, palette, 768);
    SetDDPalette(palette);
}

void I_UpdateNoBlit(void)
{
}

void I_ReadScreen(byte* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

// Copy the game framebuffer into the offscreen DirectDraw surface.
// If the surface is not 8 bit, palette indices are expanded to RGB
// during the copy - converting blits cannot be relied upon.
static boolean CopyToSurface(LPDIRECTDRAWSURFACE lpDDS, byte* source)
{
    DDSURFACEDESC	ddsd;
    byte*		dest;
    byte*		pal = gamePalette;
    byte		palWork[768];
    int			y, x;

    // Debug "Partial" palette: dim everything past the first 16
    // entries, so palette problems become obvious.
    if (fPartialPalette)
    {
	memcpy(palWork, gamePalette, 768);
	for (x = 16 ; x < 256 ; x++)
	{
	    palWork[x * 3]     /= 4;
	    palWork[x * 3 + 1] /= 4;
	    palWork[x * 3 + 2] /= 4;
	}
	pal = palWork;
    }

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    if (lpDDS->Lock(NULL, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR,
		    NULL) != DD_OK)
	return false;

    dest = (byte*)ddsd.lpSurface;

    if (iDestBPP == 8)
    {
	for (y = 0 ; y < SCREENHEIGHT ; y++)
	{
	    memcpy(dest, source + y * SCREENWIDTH, SCREENWIDTH);
	    dest += ddsd.lPitch;
	}
    }
    else if (iDestBPP == 32)
    {
	for (y = 0 ; y < SCREENHEIGHT ; y++)
	{
	    unsigned*	d = (unsigned*)(dest + y * ddsd.lPitch);
	    byte*	s = source + y * SCREENWIDTH;

	    for (x = 0 ; x < SCREENWIDTH ; x++)
	    {
		int	i = s[x] * 3;
		d[x] = ((pal[i]     * iRMax / 255) << iRShift) |
		       ((pal[i + 1] * iGMax / 255) << iGShift) |
		       ((pal[i + 2] * iBMax / 255) << iBShift);
	    }
	}
    }
    else // 16 bpp and others
    {
	for (y = 0 ; y < SCREENHEIGHT ; y++)
	{
	    unsigned short*	d = (unsigned short*)(dest + y * ddsd.lPitch);
	    byte*		s = source + y * SCREENWIDTH;

	    for (x = 0 ; x < SCREENWIDTH ; x++)
	    {
		int	i = s[x] * 3;
		d[x] = (unsigned short)
		       (((pal[i]     * iRMax / 255) << iRShift) |
			((pal[i + 1] * iGMax / 255) << iGShift) |
			((pal[i + 2] * iBMax / 255) << iBShift));
	    }
	}
    }

    lpDDS->Unlock(NULL);
    return true;
}

void I_FinishUpdate(void)
{
    if (!fAppActive)
	return;

    // Everything goes through the 8 bit offscreen surface first;
    // the blit to the (possibly hi-color) destination does the
    // palette expansion.
    if (!CopyToSurface(lpDDSOff, screens[0]))
	return;

    if (fInFullScreen)
    {
	if (fStretchOut)
	{
	    RECT	rc;

	    rc.left = 0;
	    rc.top = 0;
	    rc.right = iBackWidth;
	    rc.bottom = iBackHeight;
	    lpDDSBack->Blt(&rc, lpDDSOff, NULL, DDBLT_WAIT, NULL);
	}
	else
	{
	    lpDDSBack->Blt(NULL, lpDDSOff, NULL, DDBLT_WAIT, NULL);
	}
	lpDDSPrimary->Flip(NULL, DDFLIP_WAIT);
    }
    else
    {
	// The primary surface spans the whole desktop, so the
	// destination rectangle must be translated to the window's
	// client position on screen.
	POINT	pt;
	RECT	rc;

	pt.x = 0;
	pt.y = 0;
	ClientToScreen(hwndMain, &pt);

	rc.left = rcStretch.left + pt.x;
	rc.top = rcStretch.top + pt.y;
	rc.right = rcStretch.right + pt.x;
	rc.bottom = rcStretch.bottom + pt.y;

	lpDDSPrimary->Blt(&rc, lpDDSOff, NULL, DDBLT_WAIT, NULL);
    }
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

// ---------------------------------------------------------------------------
// Paths
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

// ---------------------------------------------------------------------------
// Network - single player only in this phase.
// DirectPlay multiplayer comes later, as in the original i_w95gdk.cpp.
// ---------------------------------------------------------------------------

void I_InitNetwork(void)
{
    int	i;

    doomcom = (doomcom_t*)malloc(sizeof(*doomcom));
    memset(doomcom, 0, sizeof(*doomcom));

    i = M_CheckParm("-dup");
    if (i && i < myargc - 1)
    {
	doomcom->ticdup = myargv[i + 1][0] - '0';
	if (doomcom->ticdup < 1)
	    doomcom->ticdup = 1;
	if (doomcom->ticdup > 9)
	    doomcom->ticdup = 9;
    }
    else
	doomcom->ticdup = 1;

    doomcom->extratics = 0;

    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
}

void I_NetCmd(void)
{
    if (doomcom->numnodes > 1)
	I_Error("Network play is not available in this build");
}
