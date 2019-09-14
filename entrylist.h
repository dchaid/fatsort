/*
	FATSort, utility for sorting FAT directory structures
	Copyright (C) 2004 Boris Leidner <fatsort(at)formenos.de>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
	This file contains/describes some ADOs which are used to
	represent the structures of FAT directory entries and entry lists.
*/

#ifndef __entrylist_h__
#define __entrylist_h__

#include <sys/types.h>
#include "FAT_fs.h"

struct sLongDirEntryList {
/*
	list structures for directory entries
	list structure for a long name entry
*/
	struct sLongDirEntry *lde;
	struct sLongDirEntryList *next;
};

struct sDirEntryList {
/*
	list structure for every file with short
	name entries and long name entries
*/
	char *sname, *lname;		// short and long name strings
	struct sShortDirEntry *sde;	// short dir entry
	struct sLongDirEntryList *ldel;	// long name entries in a list
	u_int32_t entries;		// number of entries
	struct sDirEntryList *next;	// next dir entry
};

// create new dir entry list
struct sDirEntryList *
	newDirEntryList(void);

// randomize entry list
void randomizeDirEntryList(struct sDirEntryList *list, u_int32_t entries);

// create a new directory entry holder
struct sDirEntryList *
	newDirEntry(char *sname, char *lname, struct sShortDirEntry *sde, struct sLongDirEntryList *ldel, u_int32_t entries);

// insert a long directory entry to list
struct sLongDirEntryList *
	insertLongDirEntryList(struct sLongDirEntry *lde, struct sLongDirEntryList *list);

// compare two directory entries
int32_t cmpEntries(struct sDirEntryList *de1, struct sDirEntryList *de2);

// insert a directory entry into list
void insertDirEntryList(struct sDirEntryList *new, struct sDirEntryList *list);

// free dir entry list
void freeDirEntryList(struct sDirEntryList *list);

#endif // __entrylist_h__
