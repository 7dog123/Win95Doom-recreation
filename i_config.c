// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	i_config.c - the key configuration dialog, like DOOM95.
//	Recreated from the debug symbol layout: DOOMKeyName,
//	DOOMKeyCheck, RVirtKeyToDOOM, keystore, ConfigureDialogProc,
//	IsDlgConfigureMessage, ActivateConfigure, UpdateMenu,
//	ResetMouse, ShowStore, PlaceWindow, pEditWndProc.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_config.c,v 1.0 win95 Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// Windows headers first: rpcndr.h declares byte/boolean, so
// doomtype.h must not redeclare them inside this file.
#include <windows.h>

#define __BYTEBOOL__

#ifndef true
#define true	1
#define false	0
#endif

#include "doomdef.h"
#include "m_misc.h"

extern int	VirtKeyToDOOM(int vk);

//
// The key bindings, mirroring the entries in the defaults table.
//
extern int	key_right;
extern int	key_left;
extern int	key_up;
extern int	key_down;
extern int	key_strafeleft;
extern int	key_straferight;
extern int	key_fire;
extern int	key_use;
extern int	key_strafe;
extern int	key_speed;

extern int	mouseSensitivity;
extern int	mousebfire;
extern int	mousebstrafe;
extern int	mousebforward;

typedef struct
{
    char*	action;
    int*	binding;
} bind_t;

static bind_t	aBinds[] =
{
    { "Fire",		 &key_fire },
    { "Forward",	 &key_up },
    { "Backward",	 &key_down },
    { "Turn Left",	 &key_left },
    { "Turn Right",	 &key_right },
    { "Strafe Left",	 &key_strafeleft },
    { "Strafe Right",	 &key_straferight },
    { "Speed / Run",	 &key_speed },
    { "Strafe Modifier", &key_strafe },
    { "Use / Open",	 &key_use },
};
#define NUMBINDS  (int)(sizeof(aBinds)/sizeof(aBinds[0]))

// State of virtual keys, maintained while the dialog runs.
byte	keystore[256];

// When set, even Alt combinations reach the capture control.
static boolean	fFullKeyboard = false;

static HWND	hDlgConfigure = NULL;	// the modeless dialog
static HWND	hwndDlgList = NULL;
static HWND	hwndDlgEdit = NULL;
static WNDPROC	pEditWndProc = NULL;	// saved edit control procedure

static boolean	fCapturing = false;	// waiting for a new key
static int	iCaptureBind = 0;
static int	aSavedBindings[NUMBINDS];	// cancel restores these

// control ids in the dialog template
#define IDC_BINDLIST	1001
#define IDC_CAPTURE	1002
#define IDC_RESETMOUSE	1003
#define IDC_PROMPT	1004

// ---------------------------------------------------------------------------
// Key names.
// ---------------------------------------------------------------------------

// Name of a DOOM key code, for the configuration dialog.
const char* DOOMKeyName(int key)
{
    static char name[16];

    if (key >= 33 && key <= 127)
    {
	name[0] = (char)key;
	name[1] = 0;
	return name;
    }

    switch (key)
    {
      case KEY_RIGHTARROW:	return "Right Arrow";
      case KEY_LEFTARROW:	return "Left Arrow";
      case KEY_UPARROW:		return "Up Arrow";
      case KEY_DOWNARROW:	return "Down Arrow";
      case KEY_ESCAPE:		return "Escape";
      case KEY_ENTER:		return "Enter";
      case KEY_TAB:		return "Tab";
      case KEY_BACKSPACE:	return "Backspace";
      case KEY_PAUSE:		return "Pause";
      case KEY_MINUS:		return "-";
      case KEY_EQUALS:		return "=";
      case KEY_RSHIFT:		return "Shift";
      case KEY_RCTRL:		return "Ctrl";
      case KEY_RALT:		return "Alt";
      case KEY_F1:		return "F1";
      case KEY_F2:		return "F2";
      case KEY_F3:		return "F3";
      case KEY_F4:		return "F4";
      case KEY_F5:		return "F5";
      case KEY_F6:		return "F6";
      case KEY_F7:		return "F7";
      case KEY_F8:		return "F8";
      case KEY_F9:		return "F9";
      case KEY_F10:		return "F10";
      case KEY_F11:		return "F11";
      case KEY_F12:		return "F12";
      case (0x80+0x53):		return "Del";
      case (0x80+0x52):		return "Ins";
      case (0x80+0x47):		return "Home";
      case (0x80+0x4f):		return "End";
      case (0x80+0x49):		return "PgUp";
      case (0x80+0x51):		return "PgDn";
    }

    sprintf(name, "Key %d", key);
    return name;
}

// Reverse lookup: virtual key for a DOOM key code, 0 if none.
int RVirtKeyToDOOM(int doomkey)
{
    int	vk;

    for (vk = 0 ; vk < 256 ; vk++)
	if (VirtKeyToDOOM(vk) == doomkey)
	    return vk;

    return 0;
}

// Index of the binding already using a DOOM key, or -1.
int DOOMKeyCheck(int doomkey)
{
    int	i;

    for (i = 0 ; i < NUMBINDS ; i++)
	if (*aBinds[i].binding == doomkey)
	    return i;

    return -1;
}

// ---------------------------------------------------------------------------
// Dialog position persistence, through the registry like everything else.
// ---------------------------------------------------------------------------

#define CONFIG_REGKEY_PLACE "Software\\id\\Doom95\\Config"

static void ShowStore(HWND hwnd)
{
    RECT	rc;
    HKEY	hkey;

    if (!GetWindowRect(hwnd, &rc))
	return;

    if (RegCreateKeyA(HKEY_CURRENT_USER, CONFIG_REGKEY_PLACE,
		      &hkey) != ERROR_SUCCESS)
	return;

    RegSetValueExA(hkey, "dlg_x", 0, REG_DWORD,
		   (BYTE*)&rc.left, sizeof(DWORD));
    RegSetValueExA(hkey, "dlg_y", 0, REG_DWORD,
		   (BYTE*)&rc.top, sizeof(DWORD));

    RegCloseKey(hkey);
}

static void PlaceWindow(HWND hwnd)
{
    HKEY	hkey;
    DWORD	x, y, cb, type;
    boolean	ok;

    if (RegOpenKeyA(HKEY_CURRENT_USER, CONFIG_REGKEY_PLACE,
		    &hkey) != ERROR_SUCCESS)
	return;

    ok = false;
    cb = sizeof(DWORD);
    if (RegQueryValueExA(hkey, "dlg_x", NULL, &type,
			 (BYTE*)&x, &cb) == ERROR_SUCCESS &&
	cb == sizeof(DWORD))
    {
	cb = sizeof(DWORD);
	if (RegQueryValueExA(hkey, "dlg_y", NULL, &type,
			     (BYTE*)&y, &cb) == ERROR_SUCCESS &&
	    cb == sizeof(DWORD))
	    ok = true;
    }
    RegCloseKey(hkey);

    if (ok)
	SetWindowPos(hwnd, NULL, (int)x, (int)y, 0, 0,
		     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------
// Dialog contents.
// ---------------------------------------------------------------------------

static void UpdateMenu(void)
{
    char	line[64];
    int		i;

    if (!hwndDlgList)
	return;

    SendMessage(hwndDlgList, LB_RESETCONTENT, 0, 0);

    for (i = 0 ; i < NUMBINDS ; i++)
    {
	sprintf(line, "%-17s %s", aBinds[i].action,
		DOOMKeyName(*aBinds[i].binding));
	SendMessage(hwndDlgList, LB_ADDSTRING, 0, (LPARAM)line);
    }
}

// Restore the default mouse settings.
static void ResetMouse(void)
{
    mousebfire = 0;
    mousebstrafe = 1;
    mousebforward = 2;
    mouseSensitivity = 5;

    MessageBox(hDlgConfigure,
	       "Mouse buttons and sensitivity restored.",
	       "Doom95", MB_OK | MB_ICONINFORMATION);
}

static void SetPrompt(const char* text)
{
    SendDlgItemMessage(hDlgConfigure, IDC_PROMPT, WM_SETTEXT,
		       0, (LPARAM)text);
}

static void ArmCapture(int index)
{
    char	prompt[80];

    iCaptureBind = index;
    fCapturing = true;
    fFullKeyboard = true;

    sprintf(prompt, "Press the new key for \"%s\"  (Esc cancels)",
	    aBinds[index].action);
    SetPrompt(prompt);
    SetFocus(hwndDlgEdit);
}

static void DisarmCapture(void)
{
    fCapturing = false;
    fFullKeyboard = false;
    SetPrompt("Double-click an action to change its key.");
    SendMessage(hwndDlgList, LB_SETCURSEL, iCaptureBind, 0);
    SetFocus(hwndDlgList);
}

// ---------------------------------------------------------------------------
// Subclassed capture edit: swallows keystrokes while a binding is armed.
// ---------------------------------------------------------------------------

LRESULT CALLBACK CaptureEditProc(HWND hwnd, UINT message,
				 WPARAM wParam, LPARAM lParam)
{
    MSG	msg;

    switch (message)
    {
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
	keystore[wParam & 0xff] = 1;

	if (fCapturing)
	{
	    int	doomkey;

	    if (wParam == VK_ESCAPE)
	    {
		DisarmCapture();
		return 0;
	    }

	    doomkey = VirtKeyToDOOM((int)wParam);
	    if (doomkey)
	    {
		int	other = DOOMKeyCheck(doomkey);

		if (other >= 0 && other != iCaptureBind)
		{
		    // swap with the other action, so no key dies
		    *aBinds[other].binding = *aBinds[iCaptureBind].binding;
		}
		*aBinds[iCaptureBind].binding = doomkey;

		UpdateMenu();
		DisarmCapture();
	    }
	    else
	    {
		MessageBeep(MB_ICONASTERISK);
	    }
	    return 0;
	}
	break;

      case WM_KEYUP:
      case WM_SYSKEYUP:
	keystore[wParam & 0xff] = 0;
	break;

      case WM_CHAR:
	// swallow characters - keys arrive as WM_KEYDOWN
	return 0;

      case WM_CONTEXTMENU:
	// the menu key should not open a context menu here
	return 0;
    }

    if (message == WM_SYSKEYDOWN && !fFullKeyboard)
    {
	// let the dialog manager translate it
	msg.hwnd = hwnd;
	msg.message = message;
	msg.wParam = wParam;
	msg.lParam = lParam;
	IsDialogMessage(hDlgConfigure, &msg);
	return 0;
    }

    return CallWindowProc(pEditWndProc, hwnd, message, wParam, lParam);
}

// ---------------------------------------------------------------------------
// The dialog procedure.
// ---------------------------------------------------------------------------

INT_PTR CALLBACK ConfigureDialogProc(HWND hwnd, UINT message,
				     WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
      case WM_INITDIALOG:
	hDlgConfigure = hwnd;
	hwndDlgList = GetDlgItem(hwnd, IDC_BINDLIST);
	hwndDlgEdit = GetDlgItem(hwnd, IDC_CAPTURE);

	// subclass the capture edit, like DOOM95's pEditWndProc
	pEditWndProc = (WNDPROC)GetWindowLongPtr(hwndDlgEdit,
						 GWLP_WNDPROC);
	SetWindowLongPtr(hwndDlgEdit, GWLP_WNDPROC,
			 (LONG_PTR)CaptureEditProc);

	memset(keystore, 0, sizeof(keystore));
	UpdateMenu();
	SetPrompt("Double-click an action to change its key.");
	SendMessage(hwndDlgList, LB_SETCURSEL, 0, 0);
	return TRUE;

      case WM_COMMAND:
	switch (LOWORD(wParam))
	{
	  case IDC_BINDLIST:
	    if (HIWORD(wParam) == LBN_DBLCLK)
	    {
		int	sel = SendMessage(hwndDlgList, LB_GETCURSEL,
					  0, 0);

		if (sel >= 0)
		    ArmCapture(sel);
	    }
	    break;

	  case IDOK:
	    M_SaveDefaults();
	    ShowStore(hwnd);
	    DestroyWindow(hwnd);
	    break;

	  case IDCANCEL:
	    {
		int	i;

		for (i = 0 ; i < NUMBINDS ; i++)
		    *aBinds[i].binding = aSavedBindings[i];
	    }
	    ShowStore(hwnd);
	    DestroyWindow(hwnd);
	    break;

	  case IDC_RESETMOUSE:
	    ResetMouse();
	    break;
	}
	break;

      case WM_CLOSE:
	PostMessage(hwnd, WM_COMMAND, IDCANCEL, 0);
	return TRUE;

      case WM_DESTROY:
	hDlgConfigure = NULL;
	hwndDlgList = NULL;
	hwndDlgEdit = NULL;
	break;
    }

    return FALSE;
}

// ---------------------------------------------------------------------------
// Template construction and activation.
// ---------------------------------------------------------------------------

static WORD* AlignDword(WORD* p)
{
    while ((uintptr_t)p & 3)
	p++;
    return p;
}

static WORD* AddString(WORD* p, const char* s)
{
    do
    {
	*p++ = (WORD)*s;
    } while (*s++);
    return p;
}

static WORD* AddItem(WORD* p, DWORD style, DWORD exStyle,
		     short x, short y, short cx, short cy,
		     WORD id, BYTE atom, const char* title)
{
    DLGITEMTEMPLATE*	item;

    p = AlignDword(p);
    item = (DLGITEMTEMPLATE*)p;
    item->style = style;
    item->dwExtendedStyle = exStyle;
    item->x = x;
    item->y = y;
    item->cx = cx;
    item->cy = cy;
    item->id = id;

    // The template format uses exactly 18 bytes for the item
    // header - sizeof() rounds up to 20 and would corrupt the
    // following items.
    p = (WORD*)((BYTE*)item + 18);
    *p++ = 0xffff;			// ordinal follows
    *p++ = atom;			// predefined class atom
    p = AddString(p, title);		// title text
    *p++ = 0;				// no creation data
    return p;
}

void ActivateConfigure(void)
{
    // DWORD aligned - DirectDraw of all things taught us to mind
    // alignment in this project.
    static DWORD	buffer[512];
    DLGTEMPLATE*	tpl = (DLGTEMPLATE*)buffer;
    WORD*		p;
    int			i;
    int			cdit = 6;

    if (hDlgConfigure)
    {
	SetForegroundWindow(hDlgConfigure);
	return;
    }

    memset(buffer, 0, sizeof(buffer));

    tpl->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
    tpl->dwExtendedStyle = 0;
    tpl->cdit = cdit;
    tpl->x = 40;
    tpl->y = 40;
    tpl->cx = 190;
    tpl->cy = 150;

    p = (WORD*)(tpl + 1);
    *p++ = 0;				// no menu
    *p++ = 0;				// default dialog class
    p = AddString(p, "Doom95 Setup");	// caption
    *p++ = 8;				// point size
    p = AddString(p, "MS Sans Serif");

    p = AddItem(p, WS_CHILD | WS_VISIBLE | SS_LEFT,
		0, 6, 6, 178, 10,
		IDC_PROMPT, 0x0082, "Double-click an action");

    p = AddItem(p, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
		WS_GROUP | WS_TABSTOP | LBS_NOTIFY,
		WS_EX_CLIENTEDGE, 6, 20, 178, 96,
		IDC_BINDLIST, 0x0083, "");

    p = AddItem(p, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
		ES_AUTOHSCROLL,
		WS_EX_CLIENTEDGE, 6, 122, 178, 10,
		IDC_CAPTURE, 0x0081, "");

    p = AddItem(p, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON |
		WS_TABSTOP,
		0, 24, 136, 50, 12,
		IDC_RESETMOUSE, 0x0080, "Reset Mouse");

    p = AddItem(p, WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON |
		WS_TABSTOP,
		0, 88, 136, 50, 12,
		IDOK, 0x0080, "Save");

    p = AddItem(p, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON |
		WS_TABSTOP,
		0, 142, 136, 50, 12,
		IDCANCEL, 0x0080, "Cancel");

    // Unicode template dialect: every string is a WORD sequence,
    // so the W entry point must be used explicitly.
    CreateDialogIndirectParamW(GetModuleHandle(NULL),
			       (LPCDLGTEMPLATEW)tpl,
			       NULL, ConfigureDialogProc, 0);

    if (!hDlgConfigure)
	printf("ActivateConfigure: dialog creation failed (%lu)\n",
	       GetLastError());
    else
    {
	// remember the bindings so cancel can restore them
	for (i = 0 ; i < NUMBINDS ; i++)
	    aSavedBindings[i] = *aBinds[i].binding;

	ShowWindow(hDlgConfigure, SW_SHOWNORMAL);
	UpdateWindow(hDlgConfigure);

	// restore the stored position once the window is mapped
	PlaceWindow(hDlgConfigure);
    }
}

// Route messages through the modeless dialog while it is open,
// like DOOM95's IsDlgConfigureMessage.
boolean IsDlgConfigureMessage(void* pmsg)
{
    MSG*	msg = (MSG*)pmsg;

    if (hDlgConfigure)
	return IsDialogMessage(hDlgConfigure, msg) ? true : false;

    return false;
}
