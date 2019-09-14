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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>

#include "entrylist.h"
#include "options.h"
#include "errors.h"
#include "natstrcmp.h"
#include "mallocv.h"
#include "stringlist.h"
#include "endianness.h"

// random number
u_int32_t irand( u_int32_t b, u_int32_t e)
{
    double r = e - b + 1;
    return b + (u_int32_t)(r * rand()/(RAND_MAX+1.0));
}

// List functions

struct sDirEntryList * newDirEntryList(void) {
/*
	create new dir entry list
*/
	struct sDirEntryList *tmp;

	if ((tmp=malloc(sizeof(struct sDirEntryList)))==NULL) {
		stderror();
		return NULL;
	}
	memset(tmp, 0, sizeof(struct sDirEntryList));
	return tmp;
}

struct sDirEntryList *
	newDirEntry(char *sname, char *lname, struct sShortDirEntry *sde, struct sLongDirEntryList *ldel, u_int32_t entries) {
/*
	create a new directory entry holder
*/
	assert(sname != NULL);
	assert(lname != NULL);
	assert(sde != NULL);
	
	struct sDirEntryList *tmp;

	if ((tmp=malloc(sizeof(struct sDirEntryList)))==NULL) {
		stderror();
		return NULL;
	}
	if ((tmp->sname=malloc(strlen(sname)+1))==NULL) {
		stderror();
		free(tmp);
		return NULL;
	}
	strcpy(tmp->sname, sname);
	if ((tmp->lname=malloc(strlen(lname)+1))==NULL) {
		stderror();
		free(tmp->sname);
		free(tmp);
		return NULL;
	}
	strcpy(tmp->lname, lname);

	if ((tmp->sde=malloc(sizeof(struct sShortDirEntry)))==NULL) {
		stderror();
		free(tmp->lname);
		free(tmp->sname);
		free(tmp);
		return NULL;
	}
	memcpy(tmp->sde, sde, DIR_ENTRY_SIZE);
	tmp->ldel=ldel;
	tmp->entries=entries;
	tmp->next = NULL;
	return tmp;
}

struct sLongDirEntryList *
	insertLongDirEntryList(struct sLongDirEntry *lde, struct sLongDirEntryList *list) {
/*
	insert a long directory entry to list
*/

	assert(lde != NULL);

	struct sLongDirEntryList *tmp, *new;

	if ((new=malloc(sizeof(struct sLongDirEntryList)))==NULL) {
		stderror();
		return NULL;
	}
	if ((new->lde=malloc(sizeof(struct sLongDirEntry)))==NULL) {
		stderror();
		free(new);
		return NULL;
	}
	memcpy(new->lde, lde, DIR_ENTRY_SIZE);
	new->next=NULL;

	if (list != NULL) {
		tmp=list;
		while(tmp->next != NULL) {
			tmp=tmp->next;
		}
		tmp->next=new;
		return list;
	} else {
		return new;
	}
}

int32_t stripSpecialPrefixes(char *old, char *new) {
/*
	strip special prefixes "a" and "the"
*/
	assert(old != NULL);
	assert(new != NULL);

	struct sStringList *prefix=OPT_IGNORE_PREFIXES_LIST;
	
	int32_t len, len_old;
	
	len_old=strlen(old);
	
	while(prefix->next != NULL) {
		len=strlen(prefix->next->str);
		DEBUGMSG("prefix: %s", prefix->next->str); 
		if (strncasecmp(old, prefix->next->str, len) == 0) {
			strncpy(new, old+len, len_old-len);
			new[len_old-len] = '\0';
			return 1;
		}
		prefix=prefix->next;
	}
	
	return 0;
}

int32_t cmpEntries(struct sDirEntryList *de1, struct sDirEntryList *de2) {
/*
	compare two directory entries
*/

	assert(de1 != NULL);
	assert(de2 != NULL);
	
	char s1[MAX_PATH_LEN+1];
	char s2[MAX_PATH_LEN+1];
	char s1_col[MAX_PATH_LEN*2+1];
	char s2_col[MAX_PATH_LEN*2+1];
	char scase1[MAX_PATH_LEN+1];
	char scase2[MAX_PATH_LEN+1];

	u_int16_t i;

	// the volume label must always remain at the beginning of the (root) directory
	if ((de1->sde->DIR_Atrr & (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_DIRECTORY)) == ATTR_VOLUME_ID) {
		return(-1);
	} else if ((de2->sde->DIR_Atrr & (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_DIRECTORY)) == ATTR_VOLUME_ID) {
		return(1);
	// the special "." and ".." directories must always remain at the beginning of directories, in this order
	} else if (strcmp(de1->sname, ".") == 0) {
		return(-1);
	} else if (strcmp(de2->sname, ".") == 0) {
		return(1);
	} else if (strcmp(de1->sname, "..") == 0) {
		return(-1);
	} else if (strcmp(de2->sname, "..") == 0) {
		return(1);
	// deleted entries should be moved to the end of the directory
	} else if ((u_char) de1->sname[0] == DE_FREE) {
		return(1);
	} else if ((u_char) de2->sname[0] == DE_FREE) {
		return(-1);
	}

	char *ss1,*ss2;

	if ((de1->lname != NULL) && (de1->lname[0] != '\0')) {
		ss1=de1->lname;
	} else {
		ss1=de1->sname;
	}
	if ((de2->lname != NULL) && (de2->lname[0] != '\0')) {
		ss2=de2->lname;
	} else {
		ss2=de2->sname;
	}
	
	// it's not necessary to compare files for listing and randomization,
	// each entry will be put to the end of the list
	if (OPT_LIST || OPT_RANDOM) return 1;

	// directories will be put above normal files
	if (OPT_ORDER == 0) {
		if ((de1->sde->DIR_Atrr & ATTR_DIRECTORY) &&
		   !(de2->sde->DIR_Atrr & ATTR_DIRECTORY)) {
			return -1;
		} else if (!(de1->sde->DIR_Atrr & ATTR_DIRECTORY) &&
			    (de2->sde->DIR_Atrr & ATTR_DIRECTORY)) {
			return 1;
		}
	} else if (OPT_ORDER == 1) {
		if ((de1->sde->DIR_Atrr & ATTR_DIRECTORY) &&
		   !(de2->sde->DIR_Atrr & ATTR_DIRECTORY)) {
			return 1;
		} else if (!(de1->sde->DIR_Atrr & ATTR_DIRECTORY) &&
			    (de2->sde->DIR_Atrr & ATTR_DIRECTORY)) {
			return -1;
		}
	}

	// consider last modification time
	if (OPT_MODIFICATION) {
		u_int32_t md1, md2;
		md1 = SwapInt16(de1->sde->DIR_WrtDate)<<16 | SwapInt16(de1->sde->DIR_WrtTime);
		md2 = SwapInt16(de2->sde->DIR_WrtDate)<<16 | SwapInt16(de2->sde->DIR_WrtTime);
		// printf("md1: %x, md2: %x\n", md1, md2);
		if (md1 < md2)
				return -OPT_REVERSE;
		else if (md1 > md2)
				return OPT_REVERSE;
		else return 0;
	}

	// strip special prefixes
	if (OPT_IGNORE_PREFIXES_LIST->next != NULL) {
		if (stripSpecialPrefixes(ss1, s1)) {
			ss1=s1;
		}
		if (stripSpecialPrefixes(ss2, s2)) {
			ss2=s2;
		}
	}

	//printf("Orig S1: %s, Orig S2: %s, Locale S1: %s, Locale S2: %s\n", ss1, ss2, s1_col, s2_col);

	if (OPT_IGNORE_CASE) {
		i=0;
		while(ss1[i]) {
			scase1[i] = tolower(ss1[i]);
			i++;
		}
		ss1=scase1;
		i=0;
		while(ss2[i]) {
			scase2[i] = tolower(ss2[i]);
			i++;
		}
		ss2=scase2;
	}

	if (OPT_NATURAL_SORT) {
		return natstrcmp(ss1, ss2) * OPT_REVERSE;
	} else if (OPT_ASCII) {
		// use plain ASCII corder
		return strcmp(ss1, ss2) * OPT_REVERSE;
 	} else {

		// consider locale for comparison
		if ((strxfrm(s1_col, ss1, MAX_PATH_LEN*2) == MAX_PATH_LEN*2) ||
		    (strxfrm(s2_col, ss2, MAX_PATH_LEN*2) == MAX_PATH_LEN*2)) {
			myerror("String collation error!");
			exit(1);
		}

		return strcmp(s1_col, s2_col) * OPT_REVERSE;
		
	}
}

void insertDirEntryList(struct sDirEntryList *new, struct sDirEntryList *list) {
/*
	insert a directory entry into list
*/

	assert(new != NULL);
	assert(list != NULL);

	struct sDirEntryList *tmp, *dummy;

	tmp=list;

	while ((tmp->next != NULL) &&
		(cmpEntries(new, tmp->next) >= 0)) {
		tmp=tmp->next;
	}
	
	dummy=tmp->next;
	tmp->next=new;
	new->next=dummy;
}

void freeDirEntryList(struct sDirEntryList *list) {
/*
	free dir entry list
*/

/*	char *sname, *lname;		// short and long name strings
	struct sShortDirEntry *sde;	// short dir entry
	struct sLongDirEntryList *ldel;	// long name entries in a list
	int32_t entries;		// number of entries
	struct sDirEntryList *next;	// next dir entry
*/
	assert(list != NULL);

	struct sDirEntryList *tmp;
	struct sLongDirEntryList *ldelist, *tmp2;
	
	while(list != NULL) {
		if (list->sname) free(list->sname);
		if (list->lname) free(list->lname);
		if (list->sde) free(list->sde);
		
		ldelist=list->ldel;
		while(ldelist != NULL) {
			free(ldelist->lde);
			tmp2=ldelist;
			ldelist = ldelist->next;
			free(tmp2);
		}
		
		tmp=list;
		list=list->next;
		free(tmp);
	}
}

void randomizeDirEntryList(struct sDirEntryList *list, u_int32_t entries) {
/*
	randomize entry list
*/
	assert(list != NULL);

	struct sDirEntryList *randlist, *tmp, *dummy1, *dummy2;
	u_int32_t i, j, pos;
	u_int32_t skip=0;
	
	randlist=list;
	
	// the volume label must always remain at the beginning of the (root) directory
	// the special "." and ".." directories must always remain at the beginning of directories, so skip them
	while (randlist->next &&
		(((randlist->next->sde->DIR_Atrr &
		(ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_DIRECTORY)) == ATTR_VOLUME_ID) ||
		(strcmp(randlist->next->sname, ".") == 0) ||
		(strcmp(randlist->next->sname, "..") == 0))) {

		DEBUGMSG("skipping randomization for %s", randlist->next->sname)
		randlist=randlist->next;
		skip++;
	}
	
	for (i=skip; i < entries; i++) {
		pos=irand(0, entries - 1 - i);
		
		tmp=randlist;
		// after the loop tmp->next is the selected item
		for (j=0; j<pos; j++) {
			tmp=tmp->next;
		}
		
		// put selected entry to top of list
		dummy1=tmp->next;
		tmp->next=dummy1->next;

		dummy2=randlist->next;
		randlist->next=dummy1;
		dummy1->next=dummy2;
		
		randlist=randlist->next;
	}
}
