// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// DOOM Source Code License for more details.
//
// DESCRIPTION:
//	XBAND network backend.
//
//-----------------------------------------------------------------------------

#ifndef __I_XBAND__
#define __I_XBAND__

#ifdef __GNUG__
#pragma interface
#endif

#include "doomtype.h"

// True when the XBAND transport should be tried: either the game was
// spawned by its XBUI front-end (XBUIREAD/XBUIWRITE pipe handles in
// the environment, as the original stack arranged) or -xband was
// given.  -noxband vetoes both.
boolean I_XBANDEnabled(void);

// Connect to the parent XBUI and configure doomcom for a 2 player
// session.  Returns false when the connection cannot be made.
boolean I_XBANDConnect(void);

// True between successful connect and shutdown.
boolean I_XBANDActive(void);

// doomcom->command dispatcher, called by I_NetCmd.
void I_XBANDNetCmd(void);

// Drop the XBUI connection.
void I_XBANDShutdown(void);

#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
