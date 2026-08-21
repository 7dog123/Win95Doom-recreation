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

static HBITMAP		hbmSplash = NULL;	// logo image (129)
static HBITMAP		hbmBack = NULL;		// splash background (128)
static HBITMAP		hbmProgress = NULL;	// progress bar track (126)
static HBITMAP		hbmStatus = NULL;	// progress bar fill (127)

// 24bpp conversions of the above - drawn with StretchDIBits so no
// palette mapping is ever involved
typedef struct
{
    unsigned char*	bits;
    int			w;
    int			h;
} DIB24;

static DIB24		dibBack;
static DIB24		dibSplash;
static DIB24		dibProgress;
static DIB24		dibStatus;

static RECT		progress;		// progress bar rect (client)
static RECT		debugrc;		// debug pane rect

static unsigned		uTicks = 0;		// steps completed
static unsigned		uTotalTicks = 0;	// steps overall
static unsigned		uUnits = 1;		// pixels per step
static unsigned		uNew = 0;		// pending repaint width

// rotating splash credits, original string table IDs
static const int	aCreditIDs[] =
{
    40003, 40004, 40006, 40007, 40008,
    40009, 40010, 40011, 40012, 40013, 40014, 40015
};
static int		iCredit = 0;
#define CREDIT_TIMER	1
#define CREDIT_MS	3500

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
// (BltDIBResource); we do the same via resource IDs, with a
// SPLASH.BMP next to the exe still taking precedence.
// ---------------------------------------------------------------------------

static HBITMAP LoadResourceBitmap(HINSTANCE hInst, int id)
{
    return (HBITMAP)LoadImage(hInst, MAKEINTRESOURCE(id),
			      IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
}

// convert a DIB section to a plain 24bpp top-down buffer so the
// paint path never goes through palette mapping
static int DibTo24(HBITMAP hbmp, DIB24* out)
{
    unsigned char		buf[sizeof(BITMAPINFOHEADER) + 256 * 4];
    BITMAPINFO*			pbmi = (BITMAPINFO*)buf;
    HDC				hdc;
    int				w, h, rowbytes;

    memset(out, 0, sizeof(*out));
    if (!hbmp)
	return 0;

    memset(buf, 0, sizeof(buf));
    pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    hdc = GetDC(NULL);
    if (GetDIBits(hdc, hbmp, 0, 0, NULL, pbmi, DIB_RGB_COLORS))
    {
	w = pbmi->bmiHeader.biWidth;
	h = pbmi->bmiHeader.biHeight;
	if (h < 0)
	    h = -h;
	rowbytes = ((w * 24 + 31) / 32) * 4;
	out->bits = (unsigned char*)malloc(rowbytes * h);
	pbmi->bmiHeader.biBitCount = 24;
	pbmi->bmiHeader.biCompression = BI_RGB;
	pbmi->bmiHeader.biHeight = -h;	// top-down
	pbmi->bmiHeader.biSizeImage = rowbytes * h;
	if (out->bits &&
	    GetDIBits(hdc, hbmp, 0, h, out->bits, pbmi,
		      DIB_RGB_COLORS))
	{
	    out->w = w;
	    out->h = h;
	}
	else
	{
	    free(out->bits);
	    out->bits = NULL;
	}
    }
    ReleaseDC(NULL, hdc);
    return out->bits != NULL;
}

static void LoadMyImage(HINSTANCE hInst)
{
    char	path[MAX_PATH];
    char*	lastSlash;

    // a user supplied SPLASH.BMP overrides the built-in art
    GetModuleFileName(NULL, path, MAX_PATH);
    lastSlash = strrchr(path, '\\');
    if (lastSlash)
	strcpy(lastSlash + 1, "SPLASH.BMP");
    else
	strcpy(path, "SPLASH.BMP");

    hbmSplash = (HBITMAP)LoadImage(NULL, path, IMAGE_BITMAP, 0, 0,
				   LR_LOADFROMFILE | LR_CREATEDIBSECTION);

    hbmBack = LoadResourceBitmap(hInst, 128);
    if (!hbmSplash)
	hbmSplash = LoadResourceBitmap(hInst, 129);
    hbmProgress = LoadResourceBitmap(hInst, 126);
    hbmStatus = LoadResourceBitmap(hInst, 127);

    DibTo24(hbmBack, &dibBack);
    DibTo24(hbmSplash, &dibSplash);
    DibTo24(hbmProgress, &dibProgress);
    DibTo24(hbmStatus, &dibStatus);
}

// BltDIBResource from the original: blit one of our DIBs to a
// destination rectangle.
static void BltDIBResource(HDC hdc, DIB24* dib, int x, int y,
			   int cx, int cy)
{
    BITMAPINFOHEADER	bi;

    if (!dib || !dib->bits || !cx || !cy)
	return;

    memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = dib->w;
    bi.biHeight = -dib->h;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, x, y, cx, cy,
		  0, 0, dib->w, dib->h,
		  dib->bits, (BITMAPINFO*)&bi,
		  DIB_RGB_COLORS, SRCCOPY);
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
    RECT		rc;
    HBRUSH		brush;
    int			filled;

    hdc = BeginPaint(hwnd, &ps);

    // background
    GetClientRect(hwnd, &rc);
    brush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    // splash art panel, 400x200 like the original template
    if (dibBack.bits)
	BltDIBResource(hdc, &dibBack,
		       (rc.right - 400) / 2, 6, 400, 200);
    else if (dibSplash.bits)
	BltDIBResource(hdc, &dibSplash,
		       (rc.right - 180) / 2, 6, 180, 112);
    else
	DrawLogo(hdc, 0, 10, rc.right, 150);

    // progress bar: track bitmap across the full width, then the
    // fill bitmap clipped to what's done - both original resources
    if (dibProgress.bits)
	BltDIBResource(hdc, &dibProgress,
		       progress.left, progress.top,
		       progress.right - progress.left,
		       progress.bottom - progress.top);
    else
	FrameRect(hdc, &progress, (HBRUSH)GetStockObject(WHITE_BRUSH));

    filled = (int)uNew;
    if (filled > progress.right - progress.left)
	filled = progress.right - progress.left;
    if (dibStatus.bits && filled > 4)
	BltDIBResource(hdc, &dibStatus,
		       progress.left + 2, progress.top + 3,
		       filled, progress.bottom - progress.top - 6);

    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// Window procedure.
// ---------------------------------------------------------------------------

static int OnCreate(HWND hwnd);

// show the current credit's first non-empty line
static void ShowCredit(HWND hwnd)
{
    char	buf[256];
    char*	s;
    int		len;

    buf[0] = 0;
    LoadString((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
	       aCreditIDs[iCredit], buf, sizeof(buf));
    buf[sizeof(buf) - 1] = 0;

    // first non-empty line, the pane is one line tall
    s = buf;
    while (*s == '\r' || *s == '\n')
	s++;
    len = strcspn(s, "\r\n");
    s[len] = 0;
    SetWindowText(hFinePrint, s);
}

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

      case WM_TIMER:
	// rotate the splash credits through the fine print pane
	iCredit = (iCredit + 1) % (sizeof(aCreditIDs)
				   / sizeof(aCreditIDs[0]));
	ShowCredit(hwnd);
	break;

      case WM_DESTROY:
	KillTimer(hwnd, CREDIT_TIMER);
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
    char	buf[128];
    int		cx;

    GetClientRect(hwnd, &rc);
    cx = rc.right;

    hwndTitle = CreateWindow("STATIC", "Doom95",
			     WS_CHILD | WS_VISIBLE | SS_CENTER,
			     10, 210, cx - 20, 18,
			     hwnd, NULL, NULL, NULL);
    SetGuiFont(hwndTitle);
    if (LoadString((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
		   40005, buf, sizeof(buf)))
	SetWindowText(hwndTitle, buf);

    // progress bar area, client coordinates for WM_PAINT;
    // sized for the native strip bitmap (400x23)
    progress.left = (cx - 400) / 2;
    progress.right = progress.left + 400;
    progress.top = 232;
    progress.bottom = progress.top + 23;

    hwndDebug = CreateWindow("STATIC",
			     "Starting...",
			     WS_CHILD | WS_VISIBLE | SS_LEFT |
			     WS_BORDER,
			     20, 262, cx - 40, 38,
			     hwnd, NULL, NULL, NULL);
    SetGuiFont(hwndDebug);
    debugrc.left = 20;
    debugrc.top = 262;
    debugrc.right = cx - 20;
    debugrc.bottom = 300;

    hFinePrint = CreateWindow("STATIC", "",
			      WS_CHILD | WS_VISIBLE | SS_CENTER,
			      10, 306, cx - 20, 18,
			      hwnd, NULL, NULL, NULL);
    SetGuiFont(hFinePrint);

    // start the credit rotation
    SetTimer(hwnd, CREDIT_TIMER, CREDIT_MS, NULL);
    ShowCredit(hwnd);

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

    // keep the splash alive and redraw everything (some GDI drivers
    // need a second pass before DIB blits settle)
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
	TranslateMessage(&msg);
	DispatchMessage(&msg);
    }
    InvalidateRect(hwndLaunch, NULL, FALSE);
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
    RECT	rc;
    int		cx, cy;

    LoadMyImage((HINSTANCE)hInstance);

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = LaunchWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = (HINSTANCE)hInstance;
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
    wc.hCursor = LoadCursor(NULL, IDC_WAIT);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = szLaunchClass;
    RegisterClass(&wc);

    // size the window so the CLIENT area fits the layout
    // (WS_CAPTION in the math, the visual style is the same)
    rc.left = 0;
    rc.top = 0;
    rc.right = SPLASH_CX;
    rc.bottom = SPLASH_CY;
    AdjustWindowRect(&rc, WS_POPUP | WS_CAPTION, FALSE);
    cx = rc.right - rc.left;
    cy = rc.bottom - rc.top;

    hwndLaunch = CreateWindowEx(WS_EX_TOPMOST, szLaunchClass,
				"Doom95",
				WS_POPUP | WS_DLGFRAME,
				CW_USEDEFAULT, CW_USEDEFAULT,
				cx, cy,
				NULL, NULL, (HINSTANCE)hInstance, NULL);
    if (!hwndLaunch)
	return -1;

    CenterWindow(hwndLaunch, NULL);

    ShowWindow(hwndLaunch, SW_SHOWNORMAL);
    UpdateWindow(hwndLaunch);
    // one more full paint pass; the first can come up scrambled
    // under some display drivers
    InvalidateRect(hwndLaunch, NULL, FALSE);
    UpdateWindow(hwndLaunch);

    return 0;
}
