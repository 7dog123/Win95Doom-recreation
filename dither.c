// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// dither.c
//
// Ordered-dither quantization tables for hi-color display depths.
//
// The Windows 95 desktop could be sitting at 8, 15, 16, 24 or 32
// bits per pixel when the game came up windowed; the DirectDraw
// offscreen surface inherits whatever that happens to be.  Crushing
// the 256-color palette straight into 15/16-bit channels bands
// badly on smooth ramps, so the conversion runs every palette
// entry through a 4x4 Bayer matrix: sixteen slightly different
// quantizations of each color, selected pixel-by-pixel, blend the
// error spatially and keep gradients readable on restrictive
// desktops.
//
//-----------------------------------------------------------------------------

#include <string.h>

#include "doomtype.h"
#include "dither.h"

// classic 4x4 ordered (Bayer) thresholds
static int	bayer4[16] =
{
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
};

// one packed destination pixel per source index per dither phase
static unsigned short	table16[256 * 16];
static unsigned		table32[256];

// cache key: rebuild only when palette or format changes
static byte		keyPal[768];
static int		keyRMax, keyGMax, keyBMax;
static int		keyRShift, keyGShift, keyBShift;
static int		valid16, valid32;

// quantize one channel of one color against one threshold:
// pick between floor(v*(max)/255) and the next level up
static int DitherLevel (int	v,
			int	max,
			int	phase)
{
    int	up;
    int	level;
    int	rem;

    if (max >= 255)
	return v;			// full range needs no crushing

    up   = v * max;			// scaled numerator
    level = up / 255;			// floor level
    rem  = up - level * 255;		// distance up to next level

    // bump to the next level when the remainder crosses this
    // cell's Bayer threshold ((bayer+0.5)/16 of a step)
    if (rem * 32 >= (bayer4[phase] * 2 + 1) * 255)
	level++;

    return level;
}

void Dither_Update (byte*	pal,
		    int		rmax,
		    int		gmax,
		    int		bmax,
		    int		rshift,
		    int		gshift,
		    int		bshift)
{
    int	i;
    int	ph;
    int	x;
    int	y;

    if (valid16 || valid32)
    {
	if (!memcmp(keyPal, pal, 768)
	    && keyRMax == rmax && keyGMax == gmax && keyBMax == bmax
	    && keyRShift == rshift && keyGShift == gshift
	    && keyBShift == bshift)
	    return;			// tables already current
    }

    keyRMax = rmax;   keyGMax = gmax;   keyBMax = bmax;
    keyRShift = rshift; keyGShift = gshift; keyBShift = bshift;
    memcpy(keyPal, pal, 768);

    valid16 = valid32 = 0;

    // 32bpp and deeper: exact copy, no matrix in play
    if (rmax >= 255 && gmax >= 255 && bmax >= 255)
    {
	for (i = 0 ; i < 256 ; i++)
	{
	    x = i * 3;
	    table32[i] = ((unsigned)pal[x]     << rshift)
		       | ((unsigned)pal[x + 1] << gshift)
		       | ((unsigned)pal[x + 2] << bshift);
	}
	valid32 = 1;
	return;
    }

    // crush through the matrix: sixteen flavors of each entry
    for (i = 0 ; i < 256 ; i++)
    {
	for (y = 0 ; y < 4 ; y++)
	{
	    for (x = 0 ; x < 4 ; x++)
	    {
		ph = bayer4[y * 4 + x];
		table16[i * 16 + ph] =
		    (unsigned short)
		    ((DitherLevel(pal[i*3],     rmax, ph) << rshift) |
		     (DitherLevel(pal[i*3 + 1], gmax, ph) << gshift) |
		     (DitherLevel(pal[i*3 + 2], bmax, ph) << bshift));
	    }
	}
    }
    valid16 = 1;
}

unsigned short* Dither_Table16 (void)
{
    return valid16 ? table16 : 0;
}

unsigned* Dither_Table32 (void)
{
    return valid32 ? table32 : 0;
}
