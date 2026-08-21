// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	Virtual key to DOOM key mapping, as i_vkey.c did in DOOM95.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_vkey.c,v 1.0 win95 Exp $";

#include <stdio.h>

// Windows headers first: rpcndr.h declares byte/boolean, so
// doomtype.h must not redeclare them inside this file.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define __BYTEBOOL__

#include "doomdef.h"
#include "doomtype.h"

// Maps a Win32 virtual key code to a DOOM key code.
// Returns 0 (KEY_NULL is not used by doom; we use 0 as "no key")
// when the key is not used by the game.
int VirtKeyToDOOM(int vk)
{
    switch (vk)
    {
      case VK_ESCAPE:	return KEY_ESCAPE;
      case VK_RETURN:	return KEY_ENTER;
      case VK_TAB:	return KEY_TAB;
      case VK_SPACE:	return ' ';
      case VK_BACK:	return KEY_BACKSPACE;
      case VK_PAUSE:	return KEY_PAUSE;
      case VK_SUBTRACT: return KEY_MINUS;
      case VK_ADD:	return KEY_EQUALS;

      case VK_LEFT:	return KEY_LEFTARROW;
      case VK_RIGHT:	return KEY_RIGHTARROW;
      case VK_UP:	return KEY_UPARROW;
      case VK_DOWN:	return KEY_DOWNARROW;

      // The extended editing keys have no DOOM constants in
      // doomdef.h, so they are passed through as raw codes
      // above 0x80+0x59, matching the scancode convention.
      case VK_DELETE:   return (0x80+0x53);
      case VK_INSERT:   return (0x80+0x52);
      case VK_HOME:	return (0x80+0x47);
      case VK_END:	return (0x80+0x4f);
      case VK_PRIOR:	return (0x80+0x49);
      case VK_NEXT:	return (0x80+0x51);

      case VK_SHIFT:
	// Distinguish sides like DOOM95 did; the game only
	// knows right variants for shift/ctrl/alt.
	return KEY_RSHIFT;
      case VK_CONTROL:	return KEY_RCTRL;
      case VK_MENU:	return KEY_RALT;

      case VK_F1:	return KEY_F1;
      case VK_F2:	return KEY_F2;
      case VK_F3:	return KEY_F3;
      case VK_F4:	return KEY_F4;
      case VK_F5:	return KEY_F5;
      case VK_F6:	return KEY_F6;
      case VK_F7:	return KEY_F7;
      case VK_F8:	return KEY_F8;
      case VK_F9:	return KEY_F9;
      case VK_F10:	return KEY_F10;
      case VK_F11:	return KEY_F11;
      case VK_F12:	return KEY_F12;

      default:
	if (vk >= 'A' && vk <= 'Z')
	    return 'a' + (vk - 'A');
	if (vk >= '0' && vk <= '9')
	    return vk;
	break;
    }
    return 0;
}

