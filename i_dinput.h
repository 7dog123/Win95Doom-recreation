// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	DirectInput joystick support, like DOOM95's i_dinput.c.
//	Shared header.
//
//-----------------------------------------------------------------------------

#ifndef __I_DINPUT__
#define __I_DINPUT__

#ifdef __cplusplus
extern "C" {
#endif

void I_StartupJoystick(void);
void I_ShutdownDirectInput(void);
void I_ReadDirectInput(void);
void I_ForceFeedback(int strength);

#ifdef __cplusplus
}
#endif

#endif