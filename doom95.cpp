// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	doom95.cpp - WinMain, application window, DirectDraw setup,
//	screen mode management and the command line.
//	Recreated from the DOOM95 debug symbol layout.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: doom95.cpp,v 1.0 win95 Exp $";

// Windows headers first: rpcndr.h declares byte/boolean as
// unsigned char, so doomtype.h must not redeclare them here.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <string.h>

#define __BYTEBOOL__

extern "C" {
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "m_argv.h"
#include "i_system.h"
#include "i_video.h"
#include "i_launch.h"
}

// exported by i_w95gdk.cpp
extern "C" void     doom_main(void);
extern "C" boolean  I_IsFullscreen(void);
extern "C" void     I_PostKeyboardEvent(int type, int key);
extern "C" void     I_PostMouseButtonEvent(int button, boolean down);
extern "C" void     I_SetAppActive(boolean active);
extern "C" void     I_NewWindowSize(int width, int height);

extern "C" int      VirtKeyToDOOM(int vk);

void                ToggleFullScreen(void);
void                ResizeWindowToMode(void);
void                PumpMessages(void);
extern "C" void     finiObjects(void);
extern "C" void     ActivateConfigure(void);
extern "C" boolean  IsDlgConfigureMessage(void* pmsg);

// ---------------------------------------------------------------------------
// Screen modes, cycled with the +/- keys like DOOM95's SwitchScreenSize.
// ---------------------------------------------------------------------------

typedef struct
{
    int	width;
    int	height;
} screenmode;

static screenmode	aScreenModes[] =
{
    { 320, 200 },
    { 320, 240 },
    { 512, 384 },
    { 640, 400 },
};
#define NUM_SCREEN_MODES  (int)(sizeof(aScreenModes)/sizeof(aScreenModes[0]))

int			iSMPointer = 0;		// current mode index
boolean			fStretchOut = false;	// stretch to whole client area
boolean			fPartialPalette = false; // debug: Palette/Partial
boolean			fAllowWindow = true;
boolean			fBlockWM_SIZE = false;

static char		szClass[]  = "Doom95WClass";
static char		szCaption[] = "Doom95";

HWND			hwndMain = NULL;
HINSTANCE		hInstApp = NULL;
boolean			fAppActive = false;
boolean			fInFullScreen = false;

// mouse grab state shared with i_w95gdk.cpp
boolean			fMouseGrabbed = false;

// DirectDraw objects (owned here, used by i_w95gdk.cpp)
LPDIRECTDRAW		lpDD = NULL;
LPDIRECTDRAWSURFACE	lpDDSPrimary = NULL;
LPDIRECTDRAWSURFACE	lpDDSBack = NULL;
LPDIRECTDRAWSURFACE	lpDDSOff = NULL;	// 320x200x8 system memory surface
LPDIRECTDRAWPALETTE	lpDDPal = NULL;
LPDIRECTDRAWCLIPPER	lpClipper = NULL;
HRESULT			ddrval;

int			iWinPosX = CW_USEDEFAULT;
int			iWinPosY = CW_USEDEFAULT;
RECT			rcClient;

// backbuffer dimensions in fullscreen (queried at surface creation)
int			iBackWidth = SCREENWIDTH;
int			iBackHeight = SCREENHEIGHT;

// Destination surface format, used by the framebuffer copy in
// i_w95gdk.cpp. The offscreen surface always matches the format of
// whatever it is blitted onto, so no converting blit is ever needed;
// the palette expansion happens during the copy instead.
int			iDestBPP = 32;
int			iRShift = 16, iGShift = 8, iBShift = 0;
int			iRMax = 255, iGMax = 255, iBMax = 255;

extern "C" int		i_SCREENWIDTH  = SCREENWIDTH;
extern "C" int		i_SCREENHEIGHT = SCREENHEIGHT;

// ---------------------------------------------------------------------------
// Error reporting
// ---------------------------------------------------------------------------

void MessageError(char *lpText)
{
    MessageBox(hwndMain, lpText, szCaption, MB_ICONERROR | MB_OK);
}

const char* DDError(HRESULT hres)
{
    switch (hres)
    {
      case DDERR_EXCLUSIVEMODEALREADYSET:
	return "Exclusive mode was already set by another process";
      case DDERR_UNSUPPORTEDMODE:
	return "The display mode is unsupported";
      case DDERR_INVALIDMODE:
	return "The requested display mode is invalid";
      case DDERR_NOTFOUND:
	return "The requested object was not found";
      case DDERR_OUTOFMEMORY:
	return "DirectDraw is out of memory";
      case DDERR_NOEXCLUSIVEMODE:
	return "Exclusive mode was not acquired";
      default:
	return "Unspecified DirectDraw error";
    }
}

// ---------------------------------------------------------------------------
// Palette helper - build a LOGPALETTE from the game palette.
// ---------------------------------------------------------------------------

static PALETTEENTRY	pePalette[256];
byte	gamePalette[768];
static boolean	fHasPalette = false;

void SetDDPalette(byte* palette)
{
    int		i;

    memcpy(gamePalette, palette, 768);
    fHasPalette = true;

    for (i = 0 ; i < 256 ; i++)
    {
	pePalette[i].peRed   = palette[i*3+0];
	pePalette[i].peGreen = palette[i*3+1];
	pePalette[i].peBlue  = palette[i*3+2];
	pePalette[i].peFlags = PC_NOCOLLAPSE;
    }

    if (lpDDPal)
	lpDDPal->SetEntries(0, 0, 256, pePalette);
}

// ---------------------------------------------------------------------------
// Windowed / fullscreen surface creation.
//
// Fullscreen: exclusive mode, primary flipping chain at 320x200x8.
// Windowed:   offscreen 320x200x8 system surface stretched to the primary
//             through a clipper, exactly like DOOM95's CreateWindowMode.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Offscreen render target: 320x200 system memory surface.
// Used by both windowed and fullscreen paths, like DOOM95's lpDDSOff.
//
// In fullscreen the display mode is 8 bit, so the surface is pinned to
// 8 bit palettized. Windowed it inherits the desktop depth (32bpp
// today) - the framebuffer copy expands palette indices itself, since
// converting blits are unreliable.
// ---------------------------------------------------------------------------

static void ComputeChannelShift(DWORD mask, int* shift, int* maxval)
{
    int	s = 0, b = 0;

    if (mask)
    {
	while (!(mask & (1u << s)))
	    s++;
	while ((mask >> (s + b)) & 1)
	    b++;
    }
    *shift = s;
    *maxval = (1 << b) - 1;
}

static void QueryOffscreenFormat(void)
{
    DDSURFACEDESC	ddsd;

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    if (lpDDSOff->GetSurfaceDesc(&ddsd) != DD_OK)
	return;

    iDestBPP = ddsd.ddpfPixelFormat.dwRGBBitCount;
    if (iDestBPP == 8)
	return;

    ComputeChannelShift(ddsd.ddpfPixelFormat.dwRBitMask,
			&iRShift, &iRMax);
    ComputeChannelShift(ddsd.ddpfPixelFormat.dwGBitMask,
			&iGShift, &iGMax);
    ComputeChannelShift(ddsd.ddpfPixelFormat.dwBBitMask,
			&iBShift, &iBMax);
}

static boolean CreateOffscreenSurface(boolean pin8bpp)
{
    DDSURFACEDESC	ddsd;

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    ddsd.dwWidth = SCREENWIDTH;
    ddsd.dwHeight = SCREENHEIGHT;

    if (pin8bpp)
    {
	// The pixel format MUST be pinned to 8 bit palettized - without
	// this the surface inherits the desktop depth and could not be
	// blitted onto the 8 bit backbuffer without conversion.
	ddsd.dwFlags |= DDSD_PIXELFORMAT;
	ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
	ddsd.ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXED8;
	ddsd.ddpfPixelFormat.dwRGBBitCount = 8;
    }

    ddrval = lpDD->CreateSurface(&ddsd, &lpDDSOff, NULL);
    if (ddrval != DD_OK)
	return false;

    QueryOffscreenFormat();
    return true;
}

static boolean CreateFullscreenMode(void)
{
    DDSURFACEDESC	ddsd;
    DDSCAPS		ddscaps;
    PALETTEENTRY	ppe[256];

    ddrval = lpDD->SetCooperativeLevel(hwndMain,
				       DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE |
				       DDSCL_ALLOWREBOOT);
    if (ddrval != DD_OK)
    {
	MessageError("SetCooperativeLevel failed - check your DirectX installation!");
	return false;
    }

    ddrval = lpDD->SetDisplayMode(320, 200, 8);
    if (ddrval != DD_OK)
    {
	// fall back to a standard sized mode, stretched later
	ddrval = lpDD->SetDisplayMode(640, 400, 8);
	if (ddrval != DD_OK)
	    ddrval = lpDD->SetDisplayMode(640, 480, 8);
	if (ddrval != DD_OK)
	{
	    MessageError("Could not set an 8 bit display mode - check your DirectX installation!");
	    return false;
	}
	fStretchOut = true;
    }
    else
    {
	fStretchOut = false;
    }
    fInFullScreen = true;

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE |
			  DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    ddsd.dwBackBufferCount = 1;

    ddrval = lpDD->CreateSurface(&ddsd, &lpDDSPrimary, NULL);
    if (ddrval != DD_OK)
    {
	MessageError("Could not create the primary surface!");
	return false;
    }

    ddscaps.dwCaps = DDSCAPS_BACKBUFFER;
    ddrval = lpDDSPrimary->GetAttachedSurface(&ddscaps, &lpDDSBack);
    if (ddrval != DD_OK)
    {
	MessageError("Could not get the back buffer!");
	return false;
    }

    // remember the backbuffer size for the stretch blit
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    lpDDSBack->GetSurfaceDesc(&ddsd);
    iBackWidth = ddsd.dwWidth;
    iBackHeight = ddsd.dwHeight;

    if (!CreateOffscreenSurface(true))
	return false;

    memset(ppe, 0, sizeof(ppe));
    if (lpDD->CreatePalette(DDPCAPS_8BIT | DDPCAPS_ALLOW256,
			    ppe, &lpDDPal, NULL) != DD_OK)
	return false;

    lpDDSPrimary->SetPalette(lpDDPal);
    lpDDSBack->SetPalette(lpDDPal);
    lpDDSOff->SetPalette(lpDDPal);

    return true;
}

static boolean CreateWindowMode(void)
{
    DDSURFACEDESC	ddsd;
    PALETTEENTRY	ppe[256];

    ddrval = lpDD->SetCooperativeLevel(hwndMain, DDSCL_NORMAL);
    if (ddrval != DD_OK)
	return false;

    // primary for the desktop
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    ddrval = lpDD->CreateSurface(&ddsd, &lpDDSPrimary, NULL);
    if (ddrval != DD_OK)
	return false;

    // offscreen render target, matching the desktop pixel format
    if (!CreateOffscreenSurface(false))
	return false;

    // NOTE: no clipper on the primary. The original attached one to keep
    // blits inside the window while partially covered, but wine culls
    // updates against stale visibility info during startup (window not
    // mapped yet / launcher teardown), leaving permanent black bands.
    // Without a clipper every blit lands unconditionally.

    memset(ppe, 0, sizeof(ppe));
    if (lpDD->CreatePalette(DDPCAPS_8BIT | DDPCAPS_ALLOW256,
			    ppe, &lpDDPal, NULL) != DD_OK)
	return false;

    lpDDSOff->SetPalette(lpDDPal);

    return true;
}

void finiObjects(void)
{
    if (lpDD != NULL)
    {
	if (fInFullScreen)
	    lpDD->RestoreDisplayMode();
	lpDD->SetCooperativeLevel(hwndMain, DDSCL_NORMAL);
    }
    if (lpClipper != NULL) { lpClipper->Release(); lpClipper = NULL; }
    if (lpDDPal != NULL)   { lpDDPal->Release(); lpDDPal = NULL; }
    if (lpDDSOff != NULL)  { lpDDSOff->Release(); lpDDSOff = NULL; }
    if (lpDDSBack != NULL) { lpDDSBack = NULL; }
    if (lpDDSPrimary != NULL) { lpDDSPrimary->Release(); lpDDSPrimary = NULL; }
    if (lpDD != NULL)      { lpDD->Release(); lpDD = NULL; }
    fInFullScreen = false;
}

// ---------------------------------------------------------------------------
// Fullscreen toggle (Alt+Enter), like DOOM95's FlipScreenMode.
// ---------------------------------------------------------------------------

static boolean	fWantFullScreen = false;

void ToggleFullScreen(void)
{
    fWantFullScreen = !fWantFullScreen;

    finiObjects();

    if (fWantFullScreen)
    {
	if (!CreateFullscreenMode())
	{
	    // Fall back to windowed.
	    fWantFullScreen = false;
	    CreateWindowMode();
	}
    }
    else
    {
	if (!CreateWindowMode())
	{
	    fWantFullScreen = true;
	    CreateFullscreenMode();
	}
	else
	    ResizeWindowToMode();
    }

    // Reattach the game palette to the new surfaces.
    if (fHasPalette)
	SetDDPalette(gamePalette);
}

// Switch between window sizes (called from I_Responder hooks later,
// and from WM_KEYDOWN +/-).
void SwitchScreenSize(int dir)
{
    iSMPointer += dir;
    if (iSMPointer < 0)
	iSMPointer = NUM_SCREEN_MODES - 1;
    if (iSMPointer >= NUM_SCREEN_MODES)
	iSMPointer = 0;

    fBlockWM_SIZE = true;
    ResizeWindowToMode();
    fBlockWM_SIZE = false;
}

void ResizeWindowToMode(void)
{
    RECT	rc;
    int		w, h;

    w = aScreenModes[iSMPointer].width;
    h = aScreenModes[iSMPointer].height;

    rc.left = 0;
    rc.top = 0;
    rc.right = w;
    rc.bottom = h;
    AdjustWindowRectEx(&rc,
		       GetWindowLong(hwndMain, GWL_STYLE),
		       FALSE, 0);

    fBlockWM_SIZE = true;
    MoveWindow(hwndMain,
	       iWinPosX, iWinPosY,
	       rc.right - rc.left, rc.bottom - rc.top,
	       TRUE);
    fBlockWM_SIZE = false;
}

// ---------------------------------------------------------------------------
// Keyboard handling. DOOM95 used a keyboard hook (GrabKeys); we translate
// window messages instead, which behaves identically in practice.
// ---------------------------------------------------------------------------

static void HandleKey(int vk, boolean down)
{
    int	doomkey;

    // Alt+Enter toggles fullscreen, like DOOM95.
    if (vk == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))
    {
	static boolean fWasDown = false;
	if (down && !fWasDown)
	    ToggleFullScreen();
	fWasDown = down;
	return;
    }

    // Alt+C opens the key configuration dialog.
    if (vk == 'C' && (GetKeyState(VK_MENU) & 0x8000))
    {
	static boolean fWasDown = false;
	if (down && !fWasDown)
	    ActivateConfigure();
	fWasDown = down;
	return;
    }

    // +/- cycle the window size in windowed mode.
    if ((vk == VK_ADD || vk == VK_OEM_PLUS) && !I_IsFullscreen() && down)
    {
	SwitchScreenSize(1);
	return;
    }
    if ((vk == VK_SUBTRACT || vk == VK_OEM_MINUS) && !I_IsFullscreen() && down)
    {
	SwitchScreenSize(-1);
	return;
    }

    doomkey = VirtKeyToDOOM(vk);
    if (doomkey)
	I_PostKeyboardEvent(down ? ev_keydown : ev_keyup, doomkey);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
			    WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT	ps;
    HDC		hdc;

    switch (message)
    {
      case WM_CREATE:
	break;

      case WM_ACTIVATEAPP:
	fAppActive = (wParam != 0);
	I_SetAppActive(fAppActive);
	InvalidateRect(hwnd, NULL, TRUE);
	break;

      case WM_PAINT:
	hdc = BeginPaint(hwnd, &ps);
	EndPaint(hwnd, &ps);
	break;

      case WM_CLOSE:
	DestroyWindow(hwnd);
	break;

      case WM_DESTROY:
	PostQuitMessage(0);
	break;

      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
	HandleKey((int)wParam, true);
	break;

      case WM_KEYUP:
      case WM_SYSKEYUP:
	HandleKey((int)wParam, false);
	break;

      // Mouse buttons: 0=left, 1=right, 2=middle, like DOOM95.
      case WM_LBUTTONDOWN:
	I_PostMouseButtonEvent(0, true);
	return 0;
      case WM_LBUTTONUP:
	I_PostMouseButtonEvent(0, false);
	return 0;
      case WM_RBUTTONDOWN:
	I_PostMouseButtonEvent(1, true);
	return 0;
      case WM_RBUTTONUP:
	I_PostMouseButtonEvent(1, false);
	return 0;
      case WM_MBUTTONDOWN:
	I_PostMouseButtonEvent(2, true);
	return 0;
      case WM_MBUTTONUP:
	I_PostMouseButtonEvent(2, false);
	return 0;

      // Debug menu commands (Menu103), like DOOM95's -menu bar.
      case WM_COMMAND:
	switch (LOWORD(wParam))
	{
	  case 40011:			// File/Exit
	    PostMessage(hwnd, WM_CLOSE, 0, 0);
	    break;

	  case 40004:			// Palette/Full
	    fPartialPalette = false;
	    break;
	  case 40005:			// Palette/Partial
	    fPartialPalette = true;
	    break;

	  case 40006:			// Screen/Full
	    if (!I_IsFullscreen())
		ToggleFullScreen();
	    break;
	  case 40002:			// Screen/Window
	    if (I_IsFullscreen())
		ToggleFullScreen();
	    break;

	  case 40007:			// Size/320x200
	  case 40008:			// Size/320x240
	  case 40009:			// Size/512x384
	  case 40010:			// Size/640x400
	    if (!I_IsFullscreen())
	    {
		iSMPointer = LOWORD(wParam) - 40007;
		fBlockWM_SIZE = true;
		ResizeWindowToMode();
		fBlockWM_SIZE = false;
	    }
	    break;

	  case 40003:			// Options/Player Controls
	    ActivateConfigure();
	    break;
	}
	break;

      case WM_KILLFOCUS:
	I_SetAppActive(false);
	break;

      case WM_SIZE:
	if (!fBlockWM_SIZE && wParam != SIZE_MINIMIZED)
	    I_NewWindowSize(LOWORD(lParam), HIWORD(lParam));
	break;

      case WM_MOVE:
	iWinPosX = (short)LOWORD(lParam);
	iWinPosY = (short)HIWORD(lParam);
	break;

      case WM_SETCURSOR:
	if (fMouseGrabbed && LOWORD(lParam) == HTCLIENT)
	{
	    SetCursor(NULL);
	    return TRUE;
	}
	return DefWindowProc(hwnd, message, wParam, lParam);

      default:
	return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Message pump. The game loop never returns to WinMain, so messages are
// handled here, called from I_StartFrame - same as DOOM95's
// HandleAllMessages.
// ---------------------------------------------------------------------------

static boolean	fQuitPosted = false;

void PumpMessages(void)
{
    MSG	msg;

    while (!fQuitPosted && PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
	if (msg.message == WM_QUIT)
	{
	    fQuitPosted = true;
	    I_Quit();
	}
	// let the configure dialog eat its own messages
	if (IsDlgConfigureMessage(&msg))
	    continue;
	TranslateMessage(&msg);
	DispatchMessage(&msg);
    }

    if (fQuitPosted)
	I_Quit();
}

// ---------------------------------------------------------------------------
// Command line. DOOM95 had its own ParseCommandLine building myargv;
// we fill the same m_argv.c globals from GetCommandLine.
// ---------------------------------------------------------------------------

static char*	aArgv[64];
static int	iArgCount;

static void ParseCommandLine(LPSTR lpCmdLine)
{
    char*	p = lpCmdLine;
    char*	start;
    boolean	inQuotes = false;

    aArgv[iArgCount++] = (char*)"doom95.exe";

    while (*p)
    {
	while (*p == ' ' || *p == '\t')
	    p++;
	if (!*p)
	    break;

	if (*p == '"')
	{
	    inQuotes = true;
	    p++;
	}
	start = p;

	while (*p && (inQuotes || (*p != ' ' && *p != '\t')))
	{
	    if (inQuotes && *p == '"')
	    {
		inQuotes = false;
		break;
	    }
	    p++;
	}

	if (*p)
	    *p++ = 0;
	aArgv[iArgCount++] = start;
    }

    myargv = aArgv;
    myargc = iArgCount;
}

// ---------------------------------------------------------------------------
// Init and WinMain
// ---------------------------------------------------------------------------

static BOOL doInit(HINSTANCE hInstance)
{
    WNDCLASS	wc;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = szClass;
    RegisterClass(&wc);

    hwndMain = CreateWindowEx(0, szClass, szCaption,
			      WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME),
			      iWinPosX, iWinPosY,
			      320 + GetSystemMetrics(SM_CXFIXEDFRAME) * 2,
			      200 + GetSystemMetrics(SM_CYFIXEDFRAME) * 2 +
			      GetSystemMetrics(SM_CYCAPTION),
			      NULL, NULL, hInstance, NULL);
    if (!hwndMain)
	return FALSE;

    ShowWindow(hwndMain, SW_SHOWNORMAL);
    UpdateWindow(hwndMain);

    // -menu attaches the debug menu bar, like DOOM95's -menu.
    if (M_CheckParm("-menu"))
    {
	SetMenu(hwndMain, LoadMenu(hInstance, MAKEINTRESOURCE(103)));
	DrawMenuBar(hwndMain);
    }

    return TRUE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
		   LPSTR lpCmdLine, int nCmdShow)
{
    hInstApp = hInstance;
    ParseCommandLine(lpCmdLine);

    // Splash first, like DOOM95's launcher window.
    LaunchWndInit(hInstance);

    if (!doInit(hInstance))
    {
	MessageError("Failed to create the Doom95 window");
	return 1;
    }

    ddrval = DirectDrawCreate(NULL, &lpDD, NULL);
    if (ddrval != DD_OK)
    {
	MessageError("DirectDrawCreate failed - check your DirectX installation!");
	return 1;
    }

    // Start windowed; Alt+Enter switches to fullscreen.
    if (!CreateWindowMode())
    {
	MessageError("Could not create the video surfaces - check your DirectX installation!");
	return 1;
    }

    // Size the window so the CLIENT area matches the screen mode -
    // doInit only guessed the outer dimensions.
    ResizeWindowToMode();

    // The game loop only returns via exit() from I_Quit.
    atexit(finiObjects);

    // -configure opens the key configuration dialog at startup.
    if (M_CheckParm("-configure"))
	ActivateConfigure();

    doom_main();

    return 0;
}
