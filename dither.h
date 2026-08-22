// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// dither.h
//
//-----------------------------------------------------------------------------

#ifndef __DITHER_H__
#define __DITHER_H__

// byte is unsigned char everywhere; kept loose here so the header
// can be pulled into TUs that have already swallowed windows.h

// Rebuild the ordered-dither tables when palette or target pixel
// format changed; cheap once current.  pal points at 768 RGB bytes.
void Dither_Update (unsigned char*	pal,
		    int			rmax,
		    int			gmax,
		    int			bmax,
		    int			rshift,
		    int			gshift,
		    int			bshift);

// 16 entries per source index for 4x4 Bayer phases; NULL until
// Dither_Update has seen a crushing (sub-24bpp) format.
unsigned short* Dither_Table16 (void);

// one exact packed pixel per source index for truecolor targets;
// NULL until Dither_Update has seen a >=24bpp format.
unsigned* Dither_Table32 (void);

#endif
