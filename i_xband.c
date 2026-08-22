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
//	XBAND network backend.  Connects the doomcom_t transport to an
//	XBUI front-end process through libxband (see xband/), the way
//	the original XBAND service spawned DOOM95.  Matchmaking lives
//	in the front-end; this module only moves game packets.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

// Lean windows headers skip rpcndr.h, whose byte/boolean typedefs
// clash with doomtype.h inside C translation units.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "xband.h"
#include "xbandapi.h"

#include "doomdef.h"
#include "doomstat.h"
#include "d_net.h"
#include "m_argv.h"
#include "i_xband.h"

// Vanilla d_net convention: node 0 is this machine (HSendPacket(0)
// rebounds locally), so the opposing machine is always node 1.
#define XBNODE_SELF	0
#define XBNODE_PEER	1

static boolean	fConnected = false;

boolean I_XBANDEnabled(void)
{
    char	env[8];
    int		r;

    if (M_CheckParm("-noxband"))
	return false;
    if (M_CheckParm("-xband"))
	return true;

    // Spawned by XBUI: both pipe handles must be present.
    if (!GetEnvironmentVariable("XBUIREAD", env, sizeof(env)))
	return false;
    if (!GetEnvironmentVariable("XBUIWRITE", env, sizeof(env)))
	return false;
    return true;
}

boolean I_XBANDConnect(void)
{
    int		backend = M_CheckParm("-xbanddll") ? XB_BACKEND_DLL
						     : XB_BACKEND_NATIVE;
    int		rc;
    int		n;
    int		role;

    // The XBUI protocol stamps its build signature and two expected
    // values into the init payload; xbui.exe serves the historical
    // defaults (0x19960515, 2), so ask for exactly those.
    rc = XB_Connect(backend, NULL, 0x19960515, 2);
    if (rc != XBRV_OK && backend == XB_BACKEND_NATIVE
	&& rc != XBRV_NO_ENV)
	rc = XB_Connect(XB_BACKEND_DLL, NULL, 0x19960515, 2);
    if (rc != XBRV_OK)
    {
	if (rc == XBRV_NO_ENV)
	    printf("XBAND: no XBUI pipe handles in environment\n");
	else
	    printf("XBAND: handshake failed (%d)\n", rc);
	return false;
    }

    // XBAND sessions are one-on-one.  The numeric reports what the
    // front-end believes; a count below 2 just means the opponent has
    // not been seated yet - doomcom still advertises two nodes so the
    // start handshake can complete over the wire.
    n = XB_PlayerCount();
    if (n < 2)
	n = 2;
    if (n > MAXNETNODES)
	n = MAXNETNODES;

    role = XB_Role();
    if (role < 0 || role >= n)
	role = 0;

    netgame = true;
    doomcom->numnodes = (short)n;
    doomcom->numplayers = (short)n;
    doomcom->consoleplayer = (short)role;
    doomcom->drone = 0;
    doomcom->deathmatch = M_CheckParm("-deathmatch") != 0;
    fConnected = true;
    printf("XBAND: connected as player %d of %d\n", role + 1, n);
    return true;
}

boolean I_XBANDActive(void)
{
    return fConnected;
}

void I_XBANDNetCmd(void)
{
    static byte	pkt[XB_MAX_PACKET];

    if (doomcom->command == CMD_SEND)
    {
	// Point-to-point transport: everything goes to the peer.
	XB_SendGame(&doomcom->data, doomcom->datalength);
    }
    else if (doomcom->command == CMD_GET)
    {
	doomcom->remotenode = -1;
	for (;;)
	{
	    int	r = XB_RecvGame(pkt, sizeof(pkt));
	    int	size;

	    if (r <= 0)
		break;			// queue empty or error
	    size = r > (int)sizeof(doomcom->data)
		       ? sizeof(doomcom->data) : r;
	    memcpy(&doomcom->data, pkt, size);
	    doomcom->datalength = (short)size;
	    doomcom->remotenode = XBNODE_PEER;
	    break;
	}
    }
}

void I_XBANDShutdown(void)
{
    if (fConnected)
    {
	fConnected = false;
	XB_Disconnect();
	printf("XBAND: disconnected\n");
    }
}
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
