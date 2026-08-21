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
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <dplay.h>
#include <dplobby.h>
#include <objbase.h>
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
    if (!fAppActive && !M_CheckParm("-alwaysblit"))
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
// Network - DirectPlay multiplayer, like the original i_w95gdk.cpp.
// Solo play is the default; -net enables network mode.  The dev/test
// parms -dphost and -dpjoin <address> bypass the selection dialogs;
// without them Dialog130/Dialog131 drive provider and session choice,
// like DOOM95's setup UI.
// ---------------------------------------------------------------------------

// application guid scoping Doom sessions
static GUID DOOM_GUID = {0x9b2f4a10,0x1c33,0x11d2,
			 {0x89,0x5d,0x00,0xa0,0x24,0x6e,0x98,0x17}};

static GUID TCPIP_GUID = {0x36e95ee0,0x8577,0x11cf,
			  {0x96,0x0c,0x00,0x80,0xc7,0x53,0x4e,0x82}};

static LPDIRECTPLAY3A	lpDP = NULL;		// our session
static DPID		dpidMe = 0;		// my player id
static HRESULT		dpr;			// last DirectPlay result
static DPSESSIONDESC2	sd;			// session description
static char		szPlayer[32];		// player name

// node table: remotenode -> DirectPlay player id
static DPID		aNodeDpid[MAXNETNODES];

// providers found by enumeration
typedef struct
{
    GUID	guid;
    char	name[64];
} spprovider_t;

static spprovider_t	aProviders[16];
static int		nProviders = 0;

// sessions found by enumeration
typedef struct
{
    GUID	guidInstance;
    char	name[64];
} spsession_t;

static spsession_t	aSessions[16];
static int		nSessions = 0;

// dialog state for the setup UI
static HWND		hDlgNet = NULL;
static int		iNetResult = 0;
static int		iNetSel = -1;
static boolean		fNetPickSession = false;
static char		szGameName[64] = "Doom Session";
static int		iNumDoomers = 2;

static BOOL WINAPI EnumProvidersCB(LPGUID lpGuid, LPSTR lpName,
				   DWORD dwMajor, DWORD dwMinor,
				   LPVOID lpContext)
{
    if (lpGuid && nProviders < 16)
    {
	memcpy(&aProviders[nProviders].guid, lpGuid, sizeof(GUID));
	strncpy(aProviders[nProviders].name, lpName, 63);
	aProviders[nProviders].name[63] = 0;
	nProviders++;
    }
    return TRUE;
}

static BOOL WINAPI EnumSessionsCB(LPCDPSESSIONDESC2 lpSD,
				  LPDWORD lpdwTimeOut,
				  DWORD dwFlags, LPVOID lpContext)
{
    if (lpSD && nSessions < 16)
    {
	memcpy(&aSessions[nSessions].guidInstance,
	       &lpSD->guidInstance, sizeof(GUID));
	if (lpSD->lpszSessionNameA)
	    strncpy(aSessions[nSessions].name, lpSD->lpszSessionNameA, 63);
	else
	    strcpy(aSessions[nSessions].name, "(unnamed)");
	aSessions[nSessions].name[63] = 0;
	nSessions++;
    }
    return TRUE;
}

typedef struct
{
    DPID	aDpid[MAXNETNODES];
    int		n;
} playerscan_t;

static BOOL WINAPI EnumPlayersCB(DPID dpId, DWORD dwPlayerType,
				 LPCDPNAME lpName, DWORD dwFlags,
				 LPVOID lpContext)
{
    playerscan_t*	ps = (playerscan_t*)lpContext;

    if (dpId && ps->n < MAXNETNODES)
	ps->aDpid[ps->n++] = dpId;
    return TRUE;
}

static int cmpDpid(const void* a, const void* b)
{
    DPID	da = *(DPID*)a, db = *(DPID*)b;

    return da < db ? -1 : (da > db ? 1 : 0);
}

static int I_NodeFromDpid(DPID id)
{
    int	i;

    for (i = 0 ; i < MAXNETNODES ; i++)
	if (aNodeDpid[i] == id)
	    return i;
    return -1;
}

// Assign consoleplayer and the node table: every machine sorts all
// player ids the same way, so ranks agree everywhere.  Waits until
// the roster stops changing so late joiners are counted.
static void I_NetRank(void)
{
    playerscan_t	scan;
    int			i, prev = -1, stable = 0, tries;
    int			nWant = M_CheckParm("-dphost") ? iNumDoomers : 2;

    memset(&scan, 0, sizeof(scan));
    for (tries = 0 ; tries < 60 ; tries++)
    {
	memset(&scan, 0, sizeof(scan));
	lpDP->EnumPlayers(NULL, EnumPlayersCB, &scan, 0);
	if (scan.n == prev && scan.n >= 2)
	{
	    stable++;
	    if ((scan.n >= nWant && stable >= 3) || stable >= 12)
		break;
	}
	else
	{
	    stable = 0;
	    prev = scan.n;
	}
	Sleep(500);
    }

    qsort(scan.aDpid, scan.n, sizeof(DPID), cmpDpid);
    doomcom->numnodes = scan.n;
    doomcom->numplayers = scan.n;
    // Vanilla d_net convention: this machine is always node 0 and
    // HSendPacket(0) rebounds locally; remote nodes follow in dpid
    // order.  consoleplayer is the global rank of my dpid, which
    // every machine computes identically.
    aNodeDpid[0] = dpidMe;
    doomcom->consoleplayer = 0;
    {
	int	k = 1;

	for (i = 1 ; i < MAXNETNODES ; i++)
	    aNodeDpid[i] = 0;
	for (i = 0 ; i < scan.n ; i++)
	{
	    if (scan.aDpid[i] == dpidMe)
		doomcom->consoleplayer = i;
	    else if (k < MAXNETNODES)
		aNodeDpid[k++] = scan.aDpid[i];
	}
    }
}

void I_ShutdownDP(void)
{
    if (lpDP)
    {
	lpDP->Close();
	lpDP->Release();
	lpDP = NULL;
    }
}

static boolean I_NetStartupProvider(GUID* pGuid)
{
    LPDIRECTPLAY	lpDPA = NULL;

    dpr = DirectPlayCreate(pGuid, &lpDPA, NULL);
    if (dpr != DP_OK)
	return false;
    dpr = lpDPA->QueryInterface(IID_IDirectPlay3A, (void**)&lpDP);
    lpDPA->Release();
    return dpr == DP_OK;
}

static boolean I_NetCreatePlayer(void)
{
    DPNAME	dn;

    memset(&dn, 0, sizeof(dn));
    dn.dwSize = sizeof(dn);
    dn.lpszShortNameA = szPlayer;
    dn.lpszLongNameA = szPlayer;
    dpr = lpDP->CreatePlayer(&dpidMe, &dn, NULL, NULL, 0, 0);
    return dpr == DP_OK;
}

static boolean I_NetCreateSession(const char* name, int maxplayers)
{
    memset(&sd, 0, sizeof(sd));
    sd.dwSize = sizeof(sd);
    sd.lpszSessionNameA = (char*)name;
    sd.guidApplication = DOOM_GUID;
    sd.dwMaxPlayers = maxplayers;

    dpr = lpDP->Open(&sd, DPOPEN_CREATE);
    if (dpr != DP_OK)
	return false;
    if (!I_NetCreatePlayer())
	return false;

    // print the instance guid so a joiner can target this session
    {
	DWORD		sz = 0;
	DPSESSIONDESC2*	psd;
	byte*		g;
	int		i;

	lpDP->GetSessionDesc(NULL, &sz);
	if (sz >= sizeof(*psd))
	{
	    psd = (DPSESSIONDESC2*)malloc(sz);
	    memset(psd, 0, sizeof(*psd));
	    psd->dwSize = sizeof(*psd);
	    if (lpDP->GetSessionDesc(psd, &sz) == DP_OK)
	    {
		g = (byte*)&psd->guidInstance;
		printf("I_NetHost: session guid ");
		for (i = 0 ; i < 16 ; i++)
		    printf("%02x", g[i]);
		printf("\n");
		fflush(stdout);
	    }
	    free(psd);
	}
    }
    return true;
}

static boolean ParseGuidHex(const char* s, GUID* out)
{
    byte	b[16];
    int		i;

    if (!s || strlen(s) < 32)
	return false;
    for (i = 0 ; i < 16 ; i++)
    {
	char	t[3];

	t[0] = s[i * 2];
	t[1] = s[i * 2 + 1];
	t[2] = 0;
	b[i] = (byte)strtoul(t, NULL, 16);
    }
    memcpy(out, b, 16);
    return true;
}

// Join over TCP/IP to an explicit address (and optionally an exact
// session instance guid) through the lobby API.
static boolean I_NetJoinAddress(const char* addr, const GUID* pInstance)
{
    LPDIRECTPLAYLOBBY3A	lobby = NULL;
    LPDIRECTPLAY2	dp2raw = NULL;
    unsigned char	addrbuf[128];
    DWORD		addrsize = sizeof(addrbuf);
    DPLCONNECTION	dpl;
    DPNAME		dn;
    GUID		guidINet = DPAID_INet;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    dpr = CoCreateInstance(CLSID_DirectPlayLobby, NULL,
			   CLSCTX_INPROC_SERVER,
			   IID_IDirectPlayLobby3A, (void**)&lobby);
    if (dpr != DP_OK)
	return false;

    dpr = lobby->CreateAddress(TCPIP_GUID, guidINet, (char*)addr,
			       strlen(addr) + 1, addrbuf, &addrsize);
    if (dpr != DP_OK)
    {
	lobby->Release();
	return false;
    }

    memset(&dn, 0, sizeof(dn));
    dn.dwSize = sizeof(dn);
    dn.lpszShortNameA = szPlayer;
    dn.lpszLongNameA = szPlayer;

    memset(&sd, 0, sizeof(sd));
    sd.dwSize = sizeof(sd);
    sd.guidApplication = DOOM_GUID;
    if (pInstance)
	memcpy(&sd.guidInstance, pInstance, sizeof(GUID));

    memset(&dpl, 0, sizeof(dpl));
    dpl.dwSize = sizeof(dpl);
    dpl.dwFlags = DPLCONNECTION_JOINSESSION;
    dpl.lpSessionDesc = &sd;
    dpl.lpPlayerName = &dn;
    dpl.guidSP = TCPIP_GUID;
    dpl.lpAddress = addrbuf;
    dpl.dwAddressSize = addrsize;

    dpr = lobby->SetConnectionSettings(0, 0, &dpl);
    if (dpr == DP_OK)
	dpr = lobby->Connect(0, &dp2raw, NULL);
    lobby->Release();
    if (dpr != DP_OK)
	return false;

    dpr = dp2raw->QueryInterface(IID_IDirectPlay3A, (void**)&lpDP);
    dp2raw->Release();
    if (dpr != DP_OK)
	return false;

    return I_NetCreatePlayer();
}

INT_PTR CALLBACK NetPickDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
      case WM_INITDIALOG:
	hDlgNet = hwnd;
	SendDlgItemMessage(hwnd, 1043, LB_RESETCONTENT, 0, 0);
	if (fNetPickSession)
	    SendDlgItemMessage(hwnd, 1043, LB_ADDSTRING, 0,
			       (LPARAM)"(New Game)");
	for (int i = 0 ;
	     i < (fNetPickSession ? nSessions : nProviders) ; i++)
	    SendDlgItemMessage(hwnd, 1043, LB_ADDSTRING, 0,
			       (LPARAM)(fNetPickSession ?
					aSessions[i].name :
					aProviders[i].name));
	SendDlgItemMessage(hwnd, 1043, LB_SETCURSEL, 0, 0);
	return TRUE;

      case WM_COMMAND:
	switch (LOWORD(wp))
	{
	  case IDOK:
	    iNetSel = SendDlgItemMessage(hwnd, 1043, LB_GETCURSEL, 0, 0);
	    if (iNetSel != LB_ERR)
	    {
		iNetResult = 1;
		DestroyWindow(hwnd);
	    }
	    break;
	  case IDCANCEL:
	    iNetResult = -1;
	    DestroyWindow(hwnd);
	    break;
	}
	break;

      case WM_CLOSE:
	iNetResult = -1;
	DestroyWindow(hwnd);
	break;

      case WM_DESTROY:
	hDlgNet = NULL;
	break;
    }
    return FALSE;
}

static boolean I_NetPickFromList(void)
{
    MSG	msg;

    iNetResult = 0;
    iNetSel = -1;
    CreateDialogA(GetModuleHandle(NULL), MAKEINTRESOURCE(130),
		  NULL, NetPickDlgProc);
    while (hDlgNet && GetMessageA(&msg, NULL, 0, 0) > 0)
    {
	if (!IsDialogMessage(hDlgNet, &msg))
	{
	    TranslateMessage(&msg);
	    DispatchMessageA(&msg);
	}
    }
    return iNetResult == 1;
}

INT_PTR CALLBACK NetSettingsDlgProc(HWND hwnd, UINT msg, WPARAM wp,
				    LPARAM lp)
{
    switch (msg)
    {
      case WM_INITDIALOG:
	SetDlgItemText(hwnd, 1044, szGameName);
	CheckRadioButton(hwnd, 1045, 1047,
			 iNumDoomers == 3 ? 1046 :
			 iNumDoomers == 4 ? 1047 : 1045);
	return TRUE;

      case WM_COMMAND:
	switch (LOWORD(wp))
	{
	  case IDOK:
	    GetDlgItemText(hwnd, 1044, szGameName, 63);
	    if (IsDlgButtonChecked(hwnd, 1046))
		iNumDoomers = 3;
	    else if (IsDlgButtonChecked(hwnd, 1047))
		iNumDoomers = 4;
	    else
		iNumDoomers = 2;
	    iNetResult = 1;
	    DestroyWindow(hwnd);
	    break;
	  case IDCANCEL:
	    iNetResult = -1;
	    DestroyWindow(hwnd);
	    break;
	}
	break;

      case WM_CLOSE:
	iNetResult = -1;
	DestroyWindow(hwnd);
	break;

      case WM_DESTROY:
	hDlgNet = NULL;
	break;
    }
    return FALSE;
}

static boolean I_NetAskSettings(void)
{
    MSG	msg;

    iNetResult = 0;
    CreateDialogA(GetModuleHandle(NULL), MAKEINTRESOURCE(131),
		  NULL, NetSettingsDlgProc);
    while (hDlgNet && GetMessageA(&msg, NULL, 0, 0) > 0)
    {
	if (!IsDialogMessage(hDlgNet, &msg))
	{
	    TranslateMessage(&msg);
	    DispatchMessageA(&msg);
	}
    }
    return iNetResult == 1;
}

// The full dialog driven flow: pick a connection, then host a new
// game or join one of the games found on the network.
static void I_WithPlayers(void)
{
    nProviders = 0;
    DirectPlayEnumerateA(EnumProvidersCB, NULL);
    if (!nProviders)
	I_Error("DirectPlay: no service providers found");

    fNetPickSession = false;
    if (!I_NetPickFromList())
	I_Error("Network setup cancelled");

    if (!I_NetStartupProvider(&aProviders[iNetSel].guid))
	I_Error("DirectPlay: cannot start connection");

    nSessions = 0;
    memset(&sd, 0, sizeof(sd));
    sd.dwSize = sizeof(sd);
    sd.guidApplication = DOOM_GUID;
    {
	HANDLE	ev = CreateEventA(NULL, FALSE, FALSE, NULL);

	lpDP->EnumSessions(&sd, 5000, EnumSessionsCB, ev,
			   DPENUMSESSIONS_AVAILABLE);
	CloseHandle(ev);
    }

    fNetPickSession = true;
    if (!I_NetPickFromList())
	I_Error("Network setup cancelled");

    if (iNetSel == 0)			// (New Game)
    {
	if (!I_NetAskSettings())
	    I_Error("Network setup cancelled");
	if (!I_NetCreateSession(szGameName, iNumDoomers))
	    I_Error("DirectPlay: cannot create session");
    }
    else
    {
	memcpy(&sd.guidInstance,
	       &aSessions[iNetSel - 1].guidInstance, sizeof(GUID));
	dpr = lpDP->Open(&sd, DPOPEN_JOIN);
	if (dpr != DP_OK || !I_NetCreatePlayer())
	    I_Error("DirectPlay: cannot join session");
    }
}

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
    doomcom->id = DOOMCOM_ID;

    if (!M_CheckParm("-net"))
    {
	netgame = false;
	doomcom->numplayers = doomcom->numnodes = 1;
	doomcom->deathmatch = false;
	doomcom->consoleplayer = 0;
	return;
    }

    netgame = true;
    doomcom->deathmatch = M_CheckParm("-deathmatch") != 0;
    strcpy(szPlayer, "Player");

    i = M_CheckParm("-dpname");
    if (i && i < myargc - 1)
    {
	strncpy(szPlayer, myargv[i + 1], 31);
	szPlayer[31] = 0;
    }

    i = M_CheckParm("-dpplayers");
    if (i && i < myargc - 1)
	iNumDoomers = atoi(myargv[i + 1]);
    if (iNumDoomers < 2 || iNumDoomers > 4)
	iNumDoomers = 2;

    if (M_CheckParm("-dphost"))
    {
	if (!I_NetStartupProvider(&TCPIP_GUID))
	    I_Error("DirectPlay: cannot start TCP/IP");
	if (!I_NetCreateSession(szGameName, iNumDoomers))
	    I_Error("DirectPlay: cannot create session");
    }
    else if ((i = M_CheckParm("-dpjoin")) != 0 && i < myargc - 1)
    {
	GUID	inst;
	boolean	fHaveInst = false;

	i = M_CheckParm("-dpguid");
	if (i && i < myargc - 1)
	    fHaveInst = ParseGuidHex(myargv[i + 1], &inst);
	if (!I_NetJoinAddress(myargv[M_CheckParm("-dpjoin") + 1],
			      fHaveInst ? &inst : NULL))
	    I_Error("DirectPlay: cannot join session");
    }
    else
	I_WithPlayers();

    I_NetRank();
    atexit(I_ShutdownDP);
}

void I_NetCmd(void)
{
    static byte	pkt[512];

    if (!netgame || !lpDP)
	return;

    if (doomcom->command == CMD_SEND)
    {
	lpDP->Send(dpidMe, DPID_ALLPLAYERS, 0,
		   (void*)&doomcom->data, doomcom->datalength);
    }
    else if (doomcom->command == CMD_GET)
    {
	doomcom->remotenode = -1;
	for (;;)
	{
	    DWORD	size = sizeof(pkt);
	    DPID	from = 0, to = 0;
	    int		node;

	    dpr = lpDP->Receive(&from, &to, DPRECEIVE_ALL, pkt, &size);
	    if (dpr != DP_OK)
		break;			// no more messages
	    if (from == 0 || from == dpidMe)
		continue;		// system message or echo
	    node = I_NodeFromDpid(from);
	    if (node < 0)
		continue;
	    if (size > sizeof(doomcom->data))
		size = sizeof(doomcom->data);
	    memcpy(&doomcom->data, pkt, size);
	    doomcom->datalength = size;
	    doomcom->remotenode = node;
	    break;
	}
    }
}
