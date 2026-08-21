// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	i_config.c - the Player Controls dialog, like DOOM95's
//	Dialog104. Recreated from the debug symbol layout:
//	DOOMKeyName, DOOMKeyCheck, RVirtKeyToDOOM, keystore,
//	ConfigureDialogProc, IsDlgConfigureMessage, ActivateConfigure,
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
extern int	usemouse;

// control ids in dialog resource 104
#define IDC_KEY_FWD	1018		// Forward
#define IDC_KEY_BACK	1019		// Backward
#define IDC_KEY_TURNL	1020		// Turn Left
#define IDC_KEY_TURNR	1021		// Turn Right
#define IDC_KEY_USE	1022		// Use (Open)
#define IDC_KEY_FIRE	1023		// Fire
#define IDC_KEY_SPEED	1024		// Speed On
#define IDC_KEY_STRAFE	1025		// Strafe On
#define IDC_KEY_STRAFEL	1026		// Strafe Left
#define IDC_KEY_STRAFER	1027		// Strafe Right
#define IDC_CHK_MOUSE	1013		// Use Mouse
#define IDC_MOUSE_FWD	1030		// mouse button for forward
#define IDC_MOUSE_STRAFE 1029		// mouse button for strafe
#define IDC_MOUSE_FIRE	1028		// mouse button for fire
#define IDC_CHK_ALLKEYS	1041		// allow all keys
#define IDC_APPLY	1042
#define IDC_DEFAULTS	1040
#define IDC_HELPBTN	1031

typedef struct
{
    int		id;
    int*	binding;
    HWND	hwnd;
    WNDPROC	oldproc;
} editbind_t;

// keyboard fields, in dialog order
static editbind_t aKeyEdits[] =
{
    { IDC_KEY_FWD,	 &key_up },
    { IDC_KEY_BACK,	 &key_down },
    { IDC_KEY_TURNL,	 &key_left },
    { IDC_KEY_TURNR,	 &key_right },
    { IDC_KEY_STRAFEL,	 &key_strafeleft },
    { IDC_KEY_STRAFER,	 &key_straferight },
    { IDC_KEY_SPEED,	 &key_speed },
    { IDC_KEY_STRAFE,	 &key_strafe },
    { IDC_KEY_USE,	 &key_use },
    { IDC_KEY_FIRE,	 &key_fire },
};
#define NUMKEYED  (int)(sizeof(aKeyEdits)/sizeof(aKeyEdits[0]))

// vanilla bindings, same order as aKeyEdits
static int aDefKeys[NUMKEYED] =
{
    KEY_UPARROW,
    KEY_DOWNARROW,
    KEY_LEFTARROW,
    KEY_RIGHTARROW,
    ',',
    '.',
    KEY_RSHIFT,
    KEY_RALT,
    ' ',
    KEY_RCTRL,
};

// mouse button fields
static editbind_t aMouseEdits[] =
{
    { IDC_MOUSE_FWD,	&mousebforward },
    { IDC_MOUSE_STRAFE,	&mousebstrafe },
    { IDC_MOUSE_FIRE,	&mousebfire },
};
#define NUMMOUSE  (int)(sizeof(aMouseEdits)/sizeof(aMouseEdits[0]))

// State of virtual keys, maintained while the dialog runs.
byte	keystore[256];

// When set, even Alt combinations reach the capture control.
static boolean	fFullKeyboard = false;

static HWND	hDlgConfigure = NULL;

// cancel restores these
static int	aSavedKeys[NUMKEYED];
static int	aSavedMouse[NUMMOUSE];
static int	fSavedUseMouse;

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

// Index of the keyboard binding already using a DOOM key, or -1.
int DOOMKeyCheck(int doomkey)
{
    int	i;

    for (i = 0 ; i < NUMKEYED ; i++)
	if (*aKeyEdits[i].binding == doomkey)
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
// Field display and change tracking.
// ---------------------------------------------------------------------------

static void MarkDirty(HWND hwnd)
{
    EnableWindow(GetDlgItem(hwnd, IDC_APPLY), TRUE);
}

static void ShowKeyField(int i)
{
    SetWindowText(aKeyEdits[i].hwnd, DOOMKeyName(*aKeyEdits[i].binding));
}

static void ShowMouseField(int i)
{
    char	buf[8];

    sprintf(buf, "%d", *aMouseEdits[i].binding);
    SetWindowText(aMouseEdits[i].hwnd, buf);
}

static void ShowAllFields(void)
{
    int	i;

    for (i = 0 ; i < NUMKEYED ; i++)
	ShowKeyField(i);
    for (i = 0 ; i < NUMMOUSE ; i++)
	ShowMouseField(i);
}

// Restore the default settings, like DOOM95's ResetMouse plus keys.
static void ResetDefaults(HWND hwnd)
{
    int	i;

    for (i = 0 ; i < NUMKEYED ; i++)
	*aKeyEdits[i].binding = aDefKeys[i];
    mousebfire = 0;
    mousebstrafe = 1;
    mousebforward = 2;
    mouseSensitivity = 5;

    ShowAllFields();
    MarkDirty(hwnd);
}

// ---------------------------------------------------------------------------
// Subclassed keyboard fields: select a field, press a key - assigned.
// ---------------------------------------------------------------------------

static int FindKeyEdit(HWND hwnd)
{
    int	i;

    for (i = 0 ; i < NUMKEYED ; i++)
	if (aKeyEdits[i].hwnd == hwnd)
	    return i;

    return -1;
}

static int FindMouseEdit(HWND hwnd)
{
    int	i;

    for (i = 0 ; i < NUMMOUSE ; i++)
	if (aMouseEdits[i].hwnd == hwnd)
	    return i;

    return -1;
}

LRESULT CALLBACK KeyEditProc(HWND hwnd, UINT message,
			     WPARAM wParam, LPARAM lParam)
{
    int	i = FindKeyEdit(hwnd);

    switch (message)
    {
      case WM_GETDLGCODE:
	// we want every key, including arrows and tab
	return DLGC_WANTALLKEYS | DLGC_WANTCHARS;

      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
	keystore[wParam & 0xff] = 1;

	if (message == WM_SYSKEYDOWN && !fFullKeyboard)
	    break;			// filtered, not bindable

	if (wParam != VK_ESCAPE || message == WM_SYSKEYDOWN)
	{
	    int	doomkey = VirtKeyToDOOM((int)wParam);

	    if (doomkey && i >= 0)
	    {
		int	other = DOOMKeyCheck(doomkey);

		if (other >= 0 && other != i)
		{
		    // swap with the other action, so no key dies
		    *aKeyEdits[other].binding =
			*aKeyEdits[i].binding;
		    ShowKeyField(other);
		}
		*aKeyEdits[i].binding = doomkey;
		ShowKeyField(i);
		MarkDirty(GetParent(hwnd));
	    }
	    else if (!doomkey)
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

    return CallWindowProc(aKeyEdits[i].oldproc, hwnd, message,
			  wParam, lParam);
}

// ---------------------------------------------------------------------------
// Subclassed mouse fields: click a button on the field - assigned.
// ---------------------------------------------------------------------------

LRESULT CALLBACK MouseEditProc(HWND hwnd, UINT message,
			       WPARAM wParam, LPARAM lParam)
{
    int	i = FindMouseEdit(hwnd);

    switch (message)
    {
      case WM_LBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_RBUTTONDOWN:
	if (i >= 0)
	{
	    int	btn = (message == WM_LBUTTONDOWN) ? 0 :
		       (message == WM_RBUTTONDOWN) ? 1 : 2;

	    *aMouseEdits[i].binding = btn;
	    ShowMouseField(i);
	    MarkDirty(GetParent(hwnd));
	    SetFocus(hwnd);
	}
	return 0;

      case WM_CHAR:
	return 0;

      case WM_CONTEXTMENU:
	return 0;
    }

    return CallWindowProc(aMouseEdits[i].oldproc, hwnd, message,
			  wParam, lParam);
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
      {
	  int	i;

	  hDlgConfigure = hwnd;

	  for (i = 0 ; i < NUMKEYED ; i++)
	  {
	      aKeyEdits[i].hwnd = GetDlgItem(hwnd, aKeyEdits[i].id);
	      aKeyEdits[i].oldproc = (WNDPROC)GetWindowLongPtr(
		  aKeyEdits[i].hwnd, GWLP_WNDPROC);
	      SetWindowLongPtr(aKeyEdits[i].hwnd, GWLP_WNDPROC,
			       (LONG_PTR)KeyEditProc);
	  }
	  for (i = 0 ; i < NUMMOUSE ; i++)
	  {
	      aMouseEdits[i].hwnd = GetDlgItem(hwnd, aMouseEdits[i].id);
	      aMouseEdits[i].oldproc = (WNDPROC)GetWindowLongPtr(
		  aMouseEdits[i].hwnd, GWLP_WNDPROC);
	      SetWindowLongPtr(aMouseEdits[i].hwnd, GWLP_WNDPROC,
			       (LONG_PTR)MouseEditProc);
	  }

	  memset(keystore, 0, sizeof(keystore));
	  fFullKeyboard = false;
	  ShowAllFields();
	  CheckDlgButton(hwnd, IDC_CHK_MOUSE,
			 usemouse ? BST_CHECKED : BST_UNCHECKED);
	  CheckDlgButton(hwnd, IDC_CHK_ALLKEYS, BST_UNCHECKED);

	  // remember everything so cancel can restore it
	  for (i = 0 ; i < NUMKEYED ; i++)
	      aSavedKeys[i] = *aKeyEdits[i].binding;
	  for (i = 0 ; i < NUMMOUSE ; i++)
	      aSavedMouse[i] = *aMouseEdits[i].binding;
	  fSavedUseMouse = usemouse;
      }
      return TRUE;

      case WM_COMMAND:
	switch (LOWORD(wParam))
	{
	  case IDC_CHK_MOUSE:
	    if (HIWORD(wParam) == BN_CLICKED)
	    {
		usemouse = IsDlgButtonChecked(hwnd, IDC_CHK_MOUSE)
			   == BST_CHECKED ? 1 : 0;
		MarkDirty(hwnd);
	    }
	    break;

	  case IDC_CHK_ALLKEYS:
	    if (HIWORD(wParam) == BN_CLICKED)
		fFullKeyboard = IsDlgButtonChecked(hwnd,
						   IDC_CHK_ALLKEYS)
				== BST_CHECKED ? true : false;
	    break;

	  case IDOK:
	    M_SaveDefaults();
	    ShowStore(hwnd);
	    DestroyWindow(hwnd);
	    break;

	  case IDCANCEL:
	  {
	      int	i;

	      for (i = 0 ; i < NUMKEYED ; i++)
		  *aKeyEdits[i].binding = aSavedKeys[i];
	      for (i = 0 ; i < NUMMOUSE ; i++)
		  *aMouseEdits[i].binding = aSavedMouse[i];
	      usemouse = fSavedUseMouse;
	  }
	  ShowStore(hwnd);
	  DestroyWindow(hwnd);
	  break;

	  case IDC_APPLY:
	    M_SaveDefaults();
	    EnableWindow(GetDlgItem(hwnd, IDC_APPLY), FALSE);
	    break;

	  case IDC_DEFAULTS:
	    ResetDefaults(hwnd);
	    break;

	  case IDC_HELPBTN:
	    MessageBox(hwnd,
		       "Select a keyboard field and press the key "
		       "you wish to assign.\n\n"
		       "Click a mouse button on a mouse field to "
		       "assign that button.",
		       "Player Controls", MB_OK | MB_ICONINFORMATION);
	    break;
	}
	break;

      case WM_CLOSE:
	PostMessage(hwnd, WM_COMMAND, IDCANCEL, 0);
	return TRUE;

      case WM_DESTROY:
	hDlgConfigure = NULL;
	break;
    }

    return FALSE;
}

// ---------------------------------------------------------------------------
// Activation.
// ---------------------------------------------------------------------------

void ActivateConfigure(void)
{
    if (hDlgConfigure)
    {
	SetForegroundWindow(hDlgConfigure);
	return;
    }

    {
	HWND	hdlg = CreateDialogA(GetModuleHandle(NULL),
				     MAKEINTRESOURCE(104),
				     NULL, ConfigureDialogProc);

	if (!hdlg)
	    return;
	hDlgConfigure = hdlg;

	// restore the stored position once the window is mapped
	PlaceWindow(hDlgConfigure);
	SetForegroundWindow(hDlgConfigure);
    }
}

// Route messages through the modeless dialog while it is open,
// like DOOM95's IsDlgConfigureMessage. While an entry field has
// focus every key goes to it untouched, so any key can be bound.
boolean IsDlgConfigureMessage(void* pmsg)
{
    MSG*	msg = (MSG*)pmsg;

    if (hDlgConfigure)
    {
	HWND	focus = GetFocus();

	if (focus && GetParent(focus) == hDlgConfigure)
	{
	    char	cls[8];

	    if (GetClassNameA(focus, cls, sizeof(cls)) &&
		!lstrcmpiA(cls, "edit"))
		return false;
	}

	return IsDialogMessage(hDlgConfigure, msg) ? true : false;
    }

    return false;
}
