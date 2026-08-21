// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	i_launch.cpp - the launcher splash screen, like DOOM95.
//	Recreated from the debug symbol layout: LaunchWndInit,
//	LaunchWndProc, OnCreate, hwndTitle, hwndDebug, hFinePrint,
//	hbmSplash, hbmProgress, hbmStatus, progress/debug RECTs,
//	uTicks/uUnits/uNew/uTotalTicks, LoadMyImage, LoadDIB,
//	CenterWindow, SetTick, AdvanceTick.
//
//	A SPLASH.BMP placed next to the exe is used as the logo;
//	otherwise a built-in GDI logo is drawn.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_launch.cpp,v 1.0 win95 Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "i_launch.h"

// ---------------------------------------------------------------------------
// Splash window state, mirroring the original globals.
// ---------------------------------------------------------------------------

static const char	szLaunchClass[] = "DOOM_LAUNCH";

static HWND		hwndLaunch = NULL;
static HWND		hwndTitle = NULL;	// game title text
static HWND		hwndDebug = NULL;	// boot log pane
static HWND		hFinePrint = NULL;	// legal line

static HBITMAP		hbmSplash = NULL;	// logo image
static HBITMAP		hbmProgress = NULL;	// offscreen progress bar
static HBITMAP		hbmStatus = NULL;	// segment image

static RECT		progress;		// progress bar rect (client)
static RECT		debugrc;		// debug pane rect

static unsigned		uTicks = 0;		// steps completed
static unsigned		uTotalTicks = 0;	// steps overall
static unsigned		uUnits = 1;		// pixels per step
static unsigned		uNew = 0;		// pending repaint width

#define SPLASH_CX	440
#define SPLASH_CY	330

// ---------------------------------------------------------------------------
// CenterWindow from i_win32.cpp: center one window over another.
// ---------------------------------------------------------------------------

static void CenterWindow(HWND hwnd, HWND hwndOver)
{
    RECT	rcWork, rcMe, rcOver;
    int		x, y;

    SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
    GetWindowRect(hwnd, &rcMe);

    if (hwndOver && GetWindowRect(hwndOver, &rcOver))
    {
	x = rcOver.left + ((rcOver.right - rcOver.left)
			   - (rcMe.right - rcMe.left)) / 2;
	y = rcOver.top + ((rcOver.bottom - rcOver.top)
			  - (rcMe.bottom - rcMe.top)) / 2;
    }
    else
    {
	x = rcWork.left + ((rcWork.right - rcWork.left)
			   - (rcMe.right - rcMe.left)) / 2;
	y = rcWork.top + ((rcWork.bottom - rcWork.top)
			  - (rcMe.bottom - rcMe.top)) / 2;
    }

    SetWindowPos(hwnd, NULL, x, y, 0, 0,
		 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------
// Image loading. The original pulled DIB resources from the exe
// (BltDIBResource); we load SPLASH.BMP from the exe directory so the
// art stays replaceable, with a drawn fallback.
// ---------------------------------------------------------------------------

static void LoadMyImage(void)
{
    char	path[MAX_PATH];
    char*	lastSlash;

    GetModuleFileName(NULL, path, MAX_PATH);
    lastSlash = strrchr(path, '\\');
    if (lastSlash)
	strcpy(lastSlash + 1, "SPLASH.BMP");
    else
	strcpy(path, "SPLASH.BMP");

    hbmSplash = (HBITMAP)LoadImage(NULL, path, IMAGE_BITMAP, 0, 0,
				   LR_LOADFROMFILE | LR_CREATEDIBSECTION);
}

// ---------------------------------------------------------------------------
// Painting.
// ---------------------------------------------------------------------------

static void DrawLogo(HDC hdc, int x, int y, int cx, int cy)
{
    HFONT		font;
    HFONT		oldfont;
    LOGFONT		lf;
    COLORREF		oldbk;

    // Built-in logo: big red DOOM over a white 95.
    oldbk = SetBkMode(hdc, TRANSPARENT);

    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -cy * 3 / 5;
    lf.lfWeight = FW_HEAVY;
    strcpy(lf.lfFaceName, "Arial Black");
    if (!lf.lfHeight)
	lf.lfHeight = -60;
    font = CreateFontIndirect(&lf);
    oldfont = (HFONT)SelectObject(hdc, font);

    SetTextColor(hdc, RGB(190, 20, 20));
    TextOut(hdc, x + cx / 12, y + cy / 8, "DOOM", 4);
    SetTextColor(hdc, RGB(230, 230, 230));
    TextOut(hdc, x + cx / 2 + cx / 14, y + cy * 3 / 8, "95", 2);

    SelectObject(hdc, oldfont);
    DeleteObject(font);
    SetBkColor(hdc, oldbk);
}

static void PaintLaunchWindow(HWND hwnd)
{
    PAINTSTRUCT		ps;
    HDC			hdc;
    HDC			hmem;
    BITMAP		bm;
    RECT		rc;
    HBRUSH		brush;
    int			i, filled;

    hdc = BeginPaint(hwnd, &ps);

    // background
    GetClientRect(hwnd, &rc);
    brush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    // logo area
    if (hbmSplash)
    {
	HDC		hmem;
	int		cx, cy;

	GetObject(hbmSplash, sizeof(BITMAP), &bm);
	cx = bm.bmWidth;
	if (cx > rc.right - 20)
	    cx = rc.right - 20;
	cy = bm.bmHeight * cx / bm.bmWidth;

	hmem = CreateCompatibleDC(hdc);
	SelectObject(hmem, hbmSplash);
	SetStretchBltMode(hdc, COLORONCOLOR);
	StretchBlt(hdc, (rc.right - cx) / 2, 10, cx, cy,
		   hmem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
	DeleteDC(hmem);
    }
    else
    {
	DrawLogo(hdc, 0, 10, rc.right, 150);
    }

    // progress bar frame
    FrameRect(hdc, &progress, (HBRUSH)GetStockObject(WHITE_BRUSH));

    // filled segments, like the original status bitmaps
    filled = (int)uNew;
    for (i = 0 ; i + 4 <= filled ; i += 6)
    {
	RECT	seg;

	seg.left = progress.left + 2 + i;
	seg.top = progress.top + 2;
	seg.right = seg.left + 4;
	seg.bottom = progress.bottom - 2;
	if (seg.right > progress.right - 2)
	    seg.right = progress.right - 2;
	brush = CreateSolidBrush(RGB(40, 80, 200));
	FillRect(hdc, &seg, brush);
	DeleteObject(brush);
	if (seg.right < progress.right - 2)
	    break;
    }

    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// Window procedure.
// ---------------------------------------------------------------------------

static int OnCreate(HWND hwnd);

static LRESULT CALLBACK LaunchWndProc(HWND hwnd, UINT message,
				      WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
      case WM_CREATE:
	OnCreate(hwnd);
	break;

      case WM_PAINT:
	PaintLaunchWindow(hwnd);
	break;

      case WM_DESTROY:
	hwndLaunch = NULL;
	break;

      default:
	return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Child windows: title, boot log pane and the fine print line.
// ---------------------------------------------------------------------------

static void SetGuiFont(HWND hwnd)
{
    SendMessage(hwnd, WM_SETFONT,
		(WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

static int OnCreate(HWND hwnd)
{
    RECT	rc;
    int		cx;

    GetClientRect(hwnd, &rc);
    cx = rc.right;

    hwndTitle = CreateWindow("STATIC", "Doom95",
			     WS_CHILD | WS_VISIBLE | SS_CENTER,
			     10, 165, cx - 20, 22,
			     hwnd, NULL, NULL, NULL);
    SetGuiFont(hwndTitle);

    // progress bar area, client coordinates for WM_PAINT
    progress.left = 20;
    progress.right = cx - 20;
    progress.top = 200;
    progress.bottom = 218;

    hwndDebug = CreateWindow("STATIC",
			     "Starting...",
			     WS_CHILD | WS_VISIBLE | SS_LEFT |
			     WS_BORDER,
			     20, 230, cx - 40, 62,
			     hwnd, NULL, NULL, NULL);
    SetGuiFont(hwndDebug);
    debugrc.left = 20;
    debugrc.top = 230;
    debugrc.right = cx - 20;
    debugrc.bottom = 292;

    hFinePrint = CreateWindow("STATIC",
			      "Copyright (c) 1993-1996 id Software Inc."
			      "  GNU General Public License.",
			      WS_CHILD | WS_VISIBLE | SS_CENTER,
			      10, 302, cx - 20, 18,
			      hwnd, NULL, NULL, NULL);
    SetGuiFont(hFinePrint);

    return 0;
}

// ---------------------------------------------------------------------------
// Boot log pane.
// ---------------------------------------------------------------------------

void LaunchDebug(const char* text)
{
    char	buf[1024];
    int		len;

    if (!hwndDebug)
	return;

    len = GetWindowText(hwndDebug, buf, sizeof(buf) - 1);
    if (len < 0)
	len = 0;
    buf[len] = 0;

    // keep the last few lines only
    while (len > 600)
    {
	char*	nl = strchr(buf, '\n');
	if (!nl)
	    break;
	memmove(buf, nl + 1, strlen(nl));
	len = strlen(buf);
    }

    if (len)
	strcat(buf, "\r\n");
    strcat(buf, text);
    SetWindowText(hwndDebug, buf);
}

// ---------------------------------------------------------------------------
// Progress bar protocol.
// ---------------------------------------------------------------------------

void SetTick(int total)
{
    if (total < 1)
	total = 1;

    uTotalTicks = total;
    uTicks = 0;
    uUnits = (progress.right - progress.left - 4) / uTotalTicks;
    uNew = 0;
}

void AdvanceTick(void)
{
    MSG	msg;

    if (!hwndLaunch || !uTotalTicks)
	return;

    uTicks++;
    uNew = uTicks * uUnits;

    // keep the splash alive and redraw the bar
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
	TranslateMessage(&msg);
	DispatchMessage(&msg);
    }
    InvalidateRect(hwndLaunch, &progress, FALSE);
    UpdateWindow(hwndLaunch);
}

// ---------------------------------------------------------------------------
// Creation / teardown.
// ---------------------------------------------------------------------------

void LaunchDone(void)
{
    if (hwndLaunch)
	DestroyWindow(hwndLaunch);
    hwndLaunch = NULL;
}

int LaunchWndInit(void* hInstance)
{
    WNDCLASS	wc;

    LoadMyImage();

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = LaunchWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = (HINSTANCE)hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_WAIT);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = szLaunchClass;
    RegisterClass(&wc);

    hwndLaunch = CreateWindowEx(WS_EX_TOPMOST, szLaunchClass,
				"Doom95",
				WS_POPUP | WS_DLGFRAME,
				CW_USEDEFAULT, CW_USEDEFAULT,
				SPLASH_CX, SPLASH_CY,
				NULL, NULL, (HINSTANCE)hInstance, NULL);
    if (!hwndLaunch)
	return -1;

    CenterWindow(hwndLaunch, NULL);

    ShowWindow(hwndLaunch, SW_SHOWNORMAL);
    UpdateWindow(hwndLaunch);

    return 0;
}
