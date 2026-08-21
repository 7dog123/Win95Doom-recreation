// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	Launcher splash screen, like DOOM95's i_launch.cpp.
//	Shared header (included from C and C++).
//
//-----------------------------------------------------------------------------

#ifndef __I_LAUNCH__
#define __I_LAUNCH__

#ifdef __cplusplus
extern "C" {
#endif

// Create and show the splash window. Call before anything else.
int LaunchWndInit(void* hInstance);

// Destroy the splash window; called when the game display starts.
void LaunchDone(void);

// Progress bar protocol: SetTick fixes the number of steps the
// boot will take, AdvanceTick marks one step done.
void SetTick(int total);
void AdvanceTick(void);

// Append a line to the splash debug pane.
void LaunchDebug(const char* text);

#ifdef __cplusplus
}
#endif

#endif