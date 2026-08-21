// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Win95Doom-remake
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
//
// DESCRIPTION:
//	i_dinput.c - DirectInput joystick support, like DOOM95.
//	Recreated from the debug symbol layout: I_StartupDirectInput,
//	I_ReadDirectInput, I_ShutdownDirectInput, AxisMapping,
//	SetAndCheckAxisMappings, joystickpresent, pAxisMap,
//	joy_turn/move_sensitivity, joy_turn/move_threshold,
//	joyb_* per weapon buttons, pszFeedbackDLL and the
//	gInitializeForce/gDoomForce/gUninitializeForce trio.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_dinput.c,v 1.0 win95 Exp $";

#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0500
#include <dinput.h>

// rpcndr.h already gave us byte/boolean; keep doomtype.h quiet
#define __BYTEBOOL__

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "m_misc.h"
#include "i_video.h"

// ---------------------------------------------------------------------------
// Configuration, mirroring the original globals.
// ---------------------------------------------------------------------------

extern int	usejoystick;		// from the defaults table
extern int	joybfire;		// game slots, indexed by
extern int	joybstrafe;		// G_BuildTiccmd into joybuttons[]
extern int	joybuse;
extern int	joybspeed;

static boolean	joystickpresent = 0;

// these are written by the defaults table in m_misc.c
int		joy_turn_sensitivity = 10;
int		joy_move_sensitivity = 10;
int		joy_turn_threshold = 10;	// percent of range
int		joy_move_threshold = 20;
int		joy_id = 0;
static char	pszFeedbackDLL[MAX_PATH] = "";

// per weapon select buttons, -1 = disabled; written by m_misc.c
int	joyb_fist_saw = -1;
int	joyb_pistol = -1;
int	joyb_shotgun = -1;
int	joyb_chaingun = -1;
int	joyb_missile = -1;
int	joyb_plasma = -1;
int	joyb_bfg = -1;
int	joyb_inc = -1;			// next owned weapon
int	joyb_dec = -1;			// previous owned weapon

// ---------------------------------------------------------------------------
// Device state.
// ---------------------------------------------------------------------------

static LPDIRECTINPUT		lpdi = NULL;
static LPDIRECTINPUTDEVICEA	lpdiJoy = NULL;

#define MAXJOYS	8
static GUID	aJoyGuids[MAXJOYS];
static int	iNumJoys = 0;

// axis map: [0] = turn axis, [1] = move axis, into lX lY lZ lRx lRy lRz
static int	pAxisMap[2] = { 0, 1 };
static char	szAxisMap[32] = "0 1";

static DIJOYSTATE	js;
static byte		bPrevButtons[32];
static int		iPendingKeyUp = -1;	// synthesized key awaiting release

// ---------------------------------------------------------------------------
// Force feedback. The original loaded a vendor DLL exporting
// InitializeForce / DoomForce / UninitializeForce.
// ---------------------------------------------------------------------------

static HMODULE			ghLib = NULL;
static char*			pszDLLName;
typedef HRESULT (WINAPI* INITIALIZEFORCE)(HWND);
typedef HRESULT (WINAPI* DOOMFORCE)(int);
typedef void	(WINAPI* UNINITIALIZEFORCE)(void);

static INITIALIZEFORCE		gInitializeForce;
static DOOMFORCE		gDoomForce;
static UNINITIALIZEFORCE	gUninitializeForce;

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------

extern HWND	hwndMain;
extern HINSTANCE	hInstApp;

// read a string value from the registry config, with fallback
static void GetConfigString(char* name, char* out, int outlen, char* defval)
{
    M_GetConfigString(name, out, outlen, defval);
}

// parse "turn move" axis indices, like SetAndCheckAxisMappings
static void SetAndCheckAxisMappings(void)
{
    int	i;

    for (i = 0 ; i < 2 ; i++)
    {
	int	v = 0;
	char*	s = szAxisMap;

	// skip to the i'th number
	while (*s == ' ')
	    s++;
	while (i && *s && *s != ' ')
	    s++;
	while (*s == ' ')
	    s++;

	v = atoi(s);
	if (v < 0 || v > 5)
	    v = i;			// X for turn, Y for move

	pAxisMap[i] = v;
    }
}

static long* Axis(long* base, int axis)
{
    // DIJOYSTATE layout: lX lY lZ lRx lRy lRz then more
    return base + axis;
}

// ---------------------------------------------------------------------------
// Enumeration.
// ---------------------------------------------------------------------------

static BOOL CALLBACK EnumJoysticks(LPCDIDEVICEINSTANCEA inst, LPVOID ref)
{
    if (iNumJoys < MAXJOYS)
	aJoyGuids[iNumJoys++] = inst->guidInstance;
    return DIENUM_CONTINUE;
}

// ---------------------------------------------------------------------------
// Startup / shutdown.
// ---------------------------------------------------------------------------

typedef HRESULT (WINAPI* DirectInputCreateA_t)(HINSTANCE, DWORD,
					       LPDIRECTINPUTA*, LPUNKNOWN);

void I_StartupJoystick(void)
{
    DIPROPDWORD		dz;
    DIPROPRANGE		range;
    HRESULT		hr;
    HMODULE		hDInput;
    DirectInputCreateA_t pCreate;

    if (!usejoystick || joystickpresent)
	return;

    // string settings that live outside the int defaults table
    GetConfigString("joy_axis_map", szAxisMap, sizeof(szAxisMap), "0 1");
    SetAndCheckAxisMappings();
    GetConfigString("joy_feedback_DLL", pszFeedbackDLL,
		    sizeof(pszFeedbackDLL), "");

    hDInput = LoadLibrary("dinput");
    if (!hDInput)
    {
	printf ("joystick not found\n");
	return;
    }

    pCreate = (DirectInputCreateA_t)GetProcAddress(hDInput,
						   "DirectInputCreateA");
    if (!pCreate || FAILED(pCreate((HINSTANCE)hInstApp, DIRECTINPUT_VERSION,
				   &lpdi, NULL)))
    {
	printf ("joystick not found\n");
	return;
    }

    // find what is plugged in
    iNumJoys = 0;
    lpdi->lpVtbl->EnumDevices(lpdi, DIDEVTYPE_JOYSTICK, EnumJoysticks,
			      NULL, DIEDFL_ATTACHEDONLY);

    if (!iNumJoys)
    {
	printf ("joystick not found\n");
	return;
    }

    if (joy_id < 0 || joy_id >= iNumJoys)
	joy_id = 0;

    hr = lpdi->lpVtbl->CreateDevice(lpdi, &aJoyGuids[joy_id],
				    &lpdiJoy, NULL);
    if (FAILED(hr) || !lpdiJoy)
    {
	printf ("joystick not found\n");
	return;
    }

    printf ("joystick %d found\n", joy_id);

    lpdiJoy->lpVtbl->SetDataFormat(lpdiJoy, &c_dfDIJoystick);
    lpdiJoy->lpVtbl->SetCooperativeLevel(lpdiJoy, hwndMain,
					 DISCL_NONEXCLUSIVE |
					 DISCL_FOREGROUND);

    // normalize all axes to -100..100 so thresholds are predictable
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = DIPH_BYOFFSET;
    range.lMin = -100;
    range.lMax = 100;

    dz.diph.dwSize = sizeof(dz);
    dz.diph.dwHeaderSize = sizeof(dz.diph);
    dz.diph.dwHow = DIPH_BYOFFSET;
    dz.dwData = 0;			// deadzone handled by hand

    {
	static const DWORD	aOffsets[6] =
	{
	    DIJOFS_X, DIJOFS_Y, DIJOFS_Z,
	    DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ
	};
	int		o;

	for (o = 0 ; o < 6 ; o++)
	{
	    range.diph.dwObj = aOffsets[o];
	    lpdiJoy->lpVtbl->SetProperty(lpdiJoy, DIPROP_RANGE,
					 &range.diph);
	    dz.diph.dwObj = aOffsets[o];
	    lpdiJoy->lpVtbl->SetProperty(lpdiJoy, DIPROP_DEADZONE,
					 &dz.diph);
	}
    }

    memset(bPrevButtons, 0, sizeof(bPrevButtons));

    if (FAILED(lpdiJoy->lpVtbl->Acquire(lpdiJoy)))
	lpdiJoy = NULL;
    else
	joystickpresent = 1;

    // optional force feedback support
    if (joystickpresent && pszFeedbackDLL[0])
    {
	ghLib = LoadLibrary(pszFeedbackDLL);
	if (ghLib)
	{
	    gInitializeForce = (INITIALIZEFORCE)
		GetProcAddress(ghLib, "InitializeForce");
	    gDoomForce = (DOOMFORCE)
		GetProcAddress(ghLib, "DoomForce");
	    gUninitializeForce = (UNINITIALIZEFORCE)
		GetProcAddress(ghLib, "UninitializeForce");

	    if (gInitializeForce)
		gInitializeForce(hwndMain);
	}
	else
	    printf ("couldn't load %s\n", pszFeedbackDLL);
    }
}

void I_ShutdownDirectInput(void)
{
    if (ghLib)
    {
	if (gUninitializeForce)
	    gUninitializeForce();
	FreeLibrary(ghLib);
	ghLib = NULL;
    }

    if (lpdiJoy)
    {
	lpdiJoy->lpVtbl->Unacquire(lpdiJoy);
	lpdiJoy->lpVtbl->Release(lpdiJoy);
	lpdiJoy = NULL;
    }

    if (lpdi)
    {
	lpdi->lpVtbl->Release(lpdi);
	lpdi = NULL;
    }

    joystickpresent = 0;
}

// ---------------------------------------------------------------------------
// Per weapon buttons: post the weapon's number key, the game's own
// ownership checks do the rest. One key down per tic, released the
// next, so G_BuildTiccmd always sees it.
// ---------------------------------------------------------------------------

static void PostWeaponKey(int key)
{
    event_t	ev;

    ev.type = ev_keydown;
    ev.data1 = key;
    ev.data2 = ev.data3 = 0;
    D_PostEvent(&ev);
    iPendingKeyUp = key;
}

static void CheckWeaponButtons(byte* now)
{
    player_t*	plr = &players[consoleplayer];
    int		i, w;

    // release last tic's key first
    if (iPendingKeyUp >= 0)
    {
	event_t	ev;

	ev.type = ev_keyup;
	ev.data1 = iPendingKeyUp;
	ev.data2 = ev.data3 = 0;
	D_PostEvent(&ev);
	iPendingKeyUp = -1;
    }

    if (joyb_fist_saw >= 0 && now[joyb_fist_saw] &&
	!bPrevButtons[joyb_fist_saw])
	PostWeaponKey('1');
    if (joyb_pistol >= 0 && now[joyb_pistol] &&
	!bPrevButtons[joyb_pistol])
	PostWeaponKey('2');
    if (joyb_shotgun >= 0 && now[joyb_shotgun] &&
	!bPrevButtons[joyb_shotgun])
	PostWeaponKey('3');
    if (joyb_chaingun >= 0 && now[joyb_chaingun] &&
	!bPrevButtons[joyb_chaingun])
	PostWeaponKey('4');
    if (joyb_missile >= 0 && now[joyb_missile] &&
	!bPrevButtons[joyb_missile])
	PostWeaponKey('5');
    if (joyb_plasma >= 0 && now[joyb_plasma] &&
	!bPrevButtons[joyb_plasma])
	PostWeaponKey('6');
    if (joyb_bfg >= 0 && now[joyb_bfg] && !bPrevButtons[joyb_bfg])
	PostWeaponKey('7');

    // cycle through owned weapons
    for (w = 0 ; w < 2 ; w++)
    {
	int	dir = w ? -1 : 1;
	int	btn = w ? joyb_dec : joyb_inc;

	if (btn >= 0 && now[btn] && !bPrevButtons[btn])
	{
	    for (i = 1 ; i <= NUMWEAPONS ; i++)
	    {
		int	next = (plr->readyweapon + dir * i + NUMWEAPONS)
			       % NUMWEAPONS;

		if (next < wp_chainsaw && plr->weaponowned[next])
		{
		    PostWeaponKey('1' + next);
		    break;
		}
	    }
	}
    }
}

// ---------------------------------------------------------------------------
// Read the device and feed the game, called once per tic.
// ---------------------------------------------------------------------------

void I_ReadDirectInput(void)
{
    event_t	ev;
    HRESULT	hr;
    long*	ax;
    int		turnval, moveval;
    int		i;
    int		buttonmask;

    if (!joystickpresent || !lpdiJoy)
	return;

    hr = lpdiJoy->lpVtbl->GetDeviceState(lpdiJoy, sizeof(DIJOYSTATE),
					 &js);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
    {
	if (FAILED(lpdiJoy->lpVtbl->Acquire(lpdiJoy)))
	    return;
	if (FAILED(lpdiJoy->lpVtbl->GetDeviceState(lpdiJoy,
						   sizeof(DIJOYSTATE),
						   &js)))
	    return;
    }
    else if (FAILED(hr))
	return;

    ax = Axis((long*)&js.lX, 0);

    // apply axis map, deadzone and sensitivity
    turnval = (int)ax[pAxisMap[0]];
    moveval = (int)ax[pAxisMap[1]];

    if (turnval > -joy_turn_threshold && turnval < joy_turn_threshold)
	turnval = 0;
    else
	turnval = turnval * joy_turn_sensitivity / 10;

    if (moveval > -joy_move_threshold && moveval < joy_move_threshold)
	moveval = 0;
    else
	moveval = moveval * joy_move_sensitivity / 10;

    if (turnval > 100) turnval = 100;
    if (turnval < -100) turnval = -100;
    if (moveval > 100) moveval = 100;
    if (moveval < -100) moveval = -100;

    // game slots: bit N of data1 is joybuttons[N]
    buttonmask = 0;
    if (joybfire >= 0 && joybfire < 32 && js.rgbButtons[joybfire])
	buttonmask |= 1 << joybfire;
    if (joybstrafe >= 0 && joybstrafe < 32 && js.rgbButtons[joybstrafe])
	buttonmask |= 1 << joybstrafe;
    if (joybuse >= 0 && joybuse < 32 && js.rgbButtons[joybuse])
	buttonmask |= 1 << joybuse;
    if (joybspeed >= 0 && joybspeed < 32 && js.rgbButtons[joybspeed])
	buttonmask |= 1 << joybspeed;

    CheckWeaponButtons(js.rgbButtons);

    for (i = 0 ; i < 32 ; i++)
	bPrevButtons[i] = js.rgbButtons[i];

    ev.type = ev_joystick;
    ev.data1 = buttonmask;
    ev.data2 = turnval;
    ev.data3 = moveval;
    D_PostEvent(&ev);
}

// ---------------------------------------------------------------------------
// Force feedback hook, called when the player takes damage.
// ---------------------------------------------------------------------------

void I_ForceFeedback(int strength)
{
    if (gDoomForce)
	gDoomForce(strength);
}
