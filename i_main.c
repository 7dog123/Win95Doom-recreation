// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	Main program entry called from WinMain,
//	simply calls D_DoomMain high level loop.
//	Same role as DOOM95's i_main.c.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_main.c,v 1.0 win95 Exp $";

#include <stdio.h>

#include "doomdef.h"
#include "m_argv.h"
#include "d_main.h"

void doom_main(void)
{
    // A GUI app has no console - keep stdout unbuffered so that
    // redirected output (logging) is written as it happens.
    setvbuf(stdout, NULL, _IONBF, 0);

    D_DoomMain();
}
