// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// wadread.h
//
//-----------------------------------------------------------------------------

#ifndef __WADREAD_H__
#define __WADREAD_H__

// open a WAD and load its lump directory; nonzero on success
int wadread_open (const char* path);

// release file and directory
void wadread_close (void);

int wadread_numlumps (void);

// case-insensitive lump lookup; -1 when absent
int wadread_findlump (const char* name);

int wadread_lumpsize (int index);

// read a whole lump into a caller buffer of at least lumpsize bytes
int wadread_readlump (int index, void* buffer);

// find + allocate + read in one go; free() the result when done.
// returns NULL when the lump does not exist.
void* wadread_extract (const char* name, int* size);

#endif
