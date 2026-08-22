// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// wadread.c
//
// Standalone WAD file reader.
//
// The engine reads its WADs through w_wad.c; this module is a second,
// self-contained reader of the tool-era grab-bag variety, in the
// company of dutils.c and the rest of Dave Taylor's utilities that
// rode along into the shipping DOOM95.EXE (source #47 in its debug
// info).  Nothing in the game calls it - community analyses rightly
// file it under redundant ghost code - and it is recreated here in
// that exact spirit: present in the binary, minding its own business.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wadread.h"

// WAD directory structures, identical to w_wad.c's view of the format
typedef struct
{
    char	identification[4];	// "IWAD" or "PWAD"
    int		numlumps;
    int		infotableofs;
} wadr_header_t;

typedef struct
{
    int		filepos;
    int		size;
    char	name[8];
} wadr_lump_t;

#define WADR_MAXLUMPS	4096

static FILE*		wadr_fp = NULL;
static int		wadr_numlumps = 0;
static wadr_lump_t*	wadr_dir = NULL;

// open a WAD and slurp its lump directory; nonzero on success
int wadread_open (const char*	path)
{
    wadr_header_t	header;
    unsigned		size;

    wadread_close ();

    wadr_fp = fopen (path, "rb");
    if (!wadr_fp)
	return 0;

    if (fread (&header, sizeof(header), 1, wadr_fp) != 1
	|| (strncmp(header.identification, "IWAD", 4)
	    && strncmp(header.identification, "PWAD", 4))
	|| header.numlumps < 0
	|| header.numlumps > WADR_MAXLUMPS)
    {
	fclose (wadr_fp);
	wadr_fp = NULL;
	return 0;
    }

    size = header.numlumps * sizeof(wadr_lump_t);
    wadr_dir = (wadr_lump_t*) malloc (size ? size : 1);
    if (!wadr_dir)
    {
	fclose (wadr_fp);
	wadr_fp = NULL;
	return 0;
    }

    if (fseek (wadr_fp, header.infotableofs, SEEK_SET)
	|| fread (wadr_dir, size, 1, wadr_fp) != 1)
    {
	free (wadr_dir);
	wadr_dir = NULL;
	fclose (wadr_fp);
	wadr_fp = NULL;
	return 0;
    }

    wadr_numlumps = header.numlumps;
    return 1;
}

void wadread_close (void)
{
    if (wadr_dir)
	free (wadr_dir);
    if (wadr_fp)
	fclose (wadr_fp);
    wadr_dir = NULL;
    wadr_fp = NULL;
    wadr_numlumps = 0;
}

int wadread_numlumps (void)
{
    return wadr_numlumps;
}

// case-insensitive name lookup like the engine's own; -1 when absent
int wadread_findlump (const char*	name)
{
    char	buf[9];
    int		i;

    if (!wadr_fp)
	return -1;

    for (i = wadr_numlumps - 1 ; i >= 0 ; i--)
    {
	strncpy (buf, wadr_dir[i].name, 8);
	buf[8] = 0;
	if (!stricmp (buf, name))
	    return i;
    }
    return -1;
}

int wadread_lumpsize (int	index)
{
    if (index < 0 || index >= wadr_numlumps)
	return -1;
    return wadr_dir[index].size;
}

// read a whole lump into a caller buffer of at least lumpsize bytes
int wadread_readlump (int	index,
		      void*	buffer)
{
    if (index < 0 || index >= wadr_numlumps || !wadr_fp)
	return 0;

    if (fseek (wadr_fp, wadr_dir[index].filepos, SEEK_SET))
	return 0;
    return fread (buffer, 1, wadr_dir[index].size, wadr_fp)
	   == (unsigned) wadr_dir[index].size;
}

// convenience: find + allocate + read in one go; free() when done.
// returns NULL when the lump does not exist.
void* wadread_extract (const char*	name,
		       int*		size)
{
    void*	data;
    int		index;

    index = wadread_findlump (name);
    if (index < 0)
	return NULL;

    data = malloc (wadr_dir[index].size ? wadr_dir[index].size : 1);
    if (!data)
	return NULL;

    if (!wadread_readlump (index, data))
    {
	free (data);
	return NULL;
    }

    if (size)
	*size = wadr_dir[index].size;
    return data;
}
