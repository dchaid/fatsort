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
	This file contains/describes functions for sorting of FAT filesystems.
*/

#include "sort.h"
#include "platform.h"

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/param.h>
#include "entrylist.h"
#include "errors.h"
#include "options.h"
#include "endianness.h"
#include "clusterchain.h"
#include "signal.h"
#include "misc.h"
#include "fileio.h"
#include "platform.h"
#include "stringlist.h"
#include "mallocv.h"

int32_t parseLongFilenamePart(struct sLongDirEntry *lde, char *str, iconv_t cd) {
/*
	retrieves a part of a long filename from a
	directory entry
	(thanks to M$ for this ugly hack...)
*/

	assert(lde != NULL);
	assert(str != NULL);

	size_t incount;
	size_t outcount;
	char *outptr = &(str[0]);
	char utf16str[28];
	char *inptr=&(utf16str[0]);
	size_t ret;

	str[0]='\0';

	memcpy(utf16str, (&lde->LDIR_Ord+1), 10);
	memcpy(utf16str+10, (&lde->LDIR_Ord+14), 12);
	memcpy(utf16str+22, (&lde->LDIR_Ord+28), 4);
	memset(utf16str+26, 0, 2);

	incount=26;
	outcount=MAX_PATH_LEN;

	int i;
	for (i=0;i<12; i++) {
		if ((utf16str[i*2] == '\0') && (utf16str[i*2+1] == '\0')) {
			incount=i*2;
			break;
		}
	}

	//printf("Incount: %u\n", incount);

	while (incount != 0) {
                if ((ret=iconv(cd, &inptr, &incount, &outptr, &outcount)) == (size_t)-1) {
			stderror();
                        myerror("WARNING: iconv failed!");
			break;
                }
        }
	outptr[0]='\0';

	return 0;
}

void parseShortFilename(struct sShortDirEntry *sde, char *str) {
/*
	parses short name of a file
*/

	assert(sde != NULL);
	assert(str != NULL);

	char *s;
	strncpy(str, sde->DIR_Name, 8);
	str[8]='\0';
	s=strchr(str, ' ');
	if (s!=NULL) s[0]='\0';
	if ((char)(*(sde->DIR_Name+8)) != ' ') {
		strcat(str, ".");
		strncat(str, sde->DIR_Name+8, 3);
		str[12]='\0';
	}
}

int32_t checkLongDirEntries(struct sDirEntryList *list) {
/*
	does some integrity checks on LongDirEntries
*/
	assert(list != NULL);

	u_char calculatedChecksum;
	u_int32_t i;
	u_int32_t nr;
	struct sLongDirEntryList  *tmp;

	if (list->entries > 1) {
		calculatedChecksum = calculateChecksum(list->sde->DIR_Name);
		if ((list->ldel->lde->LDIR_Ord != DE_FREE) && // ignore deleted entries
			 !(list->ldel->lde->LDIR_Ord & LAST_LONG_ENTRY)) {
			myerror("LongDirEntry should be marked as last long dir entry but isn't!");
			return -1;
		}

		tmp = list->ldel;

		for(i=0;i < list->entries - 1; i++) {
			if (tmp->lde->LDIR_Ord != DE_FREE) { // ignore deleted entries
				nr=tmp->lde->LDIR_Ord & ~LAST_LONG_ENTRY;	// index of long dir entry
				//fprintf(stderr, "Debug: count=%x, LDIR_Ord=%x\n", list->entries - 1 -i, tmp->lde->LDIR_Ord);
				if (nr != (list->entries - 1 - i)) {
					myerror("LongDirEntry number is 0x%x (0x%x) but should be 0x%x!",
						nr, tmp->lde->LDIR_Ord, list->entries - 1 - i );
					return -1;
				} else if (tmp->lde->LDIR_Checksum != calculatedChecksum) {
					myerror("Checksum for LongDirEntry is 0x%x but should be 0x%x!",
						tmp->lde->LDIR_Checksum,
						calculatedChecksum);
					return -1;
				}
			}
			tmp = tmp->next;
		}
	}

	return 0;
}

int32_t parseClusterChain(struct sFileSystem *fs, struct sClusterChain *chain, struct sDirEntryList *list, u_int32_t *direntries) {
/*
	parses a cluster chain and puts found directory entries to list
*/

	assert(fs != NULL);
	assert(chain != NULL);
	assert(list != NULL);
	assert(direntries != NULL);

	u_int32_t j;
	int32_t ret;
	u_int32_t entries=0;
	union sDirEntry de;
	struct sDirEntryList *lnde;
	struct sLongDirEntryList *llist;
	char tmp[MAX_PATH_LEN+1], dummy[MAX_PATH_LEN+1], sname[MAX_PATH_LEN+1], lname[MAX_PATH_LEN+1];

	*direntries=0;

	chain=chain->next;	// head element

	llist = NULL;
	lname[0]='\0';
	while (chain != NULL) {
		fs_seek(fs->fd, getClusterOffset(fs, chain->cluster), SEEK_SET);
		for (j=0;j<fs->maxDirEntriesPerCluster;j++) {
			entries++;
			ret=parseEntry(fs, &de);

			switch(ret) {
			case -1:
				myerror("Failed to parse directory entry!");
				return -1;
			case 0: // current dir entry and following dir entries are free
				if (llist != NULL) {
					// short dir entry is still missing!
					myerror("ShortDirEntry is missing after LongDirEntries (cluster: %08lx, entry %u)!",
						chain->cluster, j);
					return -1;
				} else {
					return 0;
				}
			case 1: // short dir entry
				parseShortFilename(&de.ShortDirEntry, sname);

				if (OPT_LIST &&
				   strcmp(sname, ".") &&
				   strcmp(sname, "..") &&
				   (((u_char) sname[0]) != DE_FREE) &&
				  !(de.ShortDirEntry.DIR_Atrr & ATTR_VOLUME_ID)) {

					if (!OPT_MORE_INFO) {
						printf("%s\n", (lname[0] != '\0') ? lname : sname);
					} else {
						printf("%s (%s)\n", (lname[0] != '\0') ? lname : "n/a", sname);
					}
				}

				lnde=newDirEntry(sname, lname, &de.ShortDirEntry, llist, entries);
				if (lnde == NULL) {
					myerror("Failed to create DirEntry!");
					return -1;
				}

				if (checkLongDirEntries(lnde)) {
					myerror("checkDirEntry failed in cluster %08lx at entry %u!", chain->cluster, j);
					return -1;
				}

				insertDirEntryList(lnde, list);
				(*direntries)++;
				entries=0;
				llist = NULL;
				lname[0]='\0';
				break;
			case 2: // long dir entry
				if (parseLongFilenamePart(&de.LongDirEntry, tmp, fs->cd)) {
					myerror("Failed to parse long filename part!");
					return -1;
				}

				// insert long dir entry in list
				llist=insertLongDirEntryList(&de.LongDirEntry, llist);
				if (llist == NULL) {
					myerror("Failed to insert LongDirEntry!");
					return -1;
				}

				strncpy(dummy, tmp, MAX_PATH_LEN);
				dummy[MAX_PATH_LEN]='\0';
				strncat(dummy, lname, MAX_PATH_LEN - strlen(dummy));
				dummy[MAX_PATH_LEN]='\0';
				strncpy(lname, dummy, MAX_PATH_LEN);
				dummy[MAX_PATH_LEN]='\0';
				break;
			default:
				myerror("Unhandled return code!");
				return -1;
			}

		}
		chain=chain->next;
	}

	if (llist != NULL) {
		// short dir entry is still missing!
		myerror("ShortDirEntry is missing after LongDirEntries (root directory entry %d)!", j);
		return -1;
	}

	return 0;
}

int32_t parseFAT1xRootDirEntries(struct sFileSystem *fs, struct sDirEntryList *list, u_int32_t *direntries) {
/*
	parses FAT1x root directory entries to list
*/

	assert(fs != NULL);
	assert(list != NULL);
	assert(direntries != NULL);

	off_t BSOffset;
	int32_t j, ret;
	u_int32_t entries=0;
	union sDirEntry de;
	struct sDirEntryList *lnde;
	struct sLongDirEntryList *llist;
	char tmp[MAX_PATH_LEN+1], dummy[MAX_PATH_LEN+1], sname[MAX_PATH_LEN+1], lname[MAX_PATH_LEN+1];

	*direntries=0;

	llist = NULL;
	lname[0]='\0';

	BSOffset = ((off_t)SwapInt16(fs->bs.BS_RsvdSecCnt) +
		fs->bs.BS_NumFATs * fs->FATSize) * fs->sectorSize;

	fs_seek(fs->fd, BSOffset, SEEK_SET);

	for (j=0;j<SwapInt16(fs->bs.BS_RootEntCnt);j++) {
		entries++;
		ret=parseEntry(fs, &de);

		switch(ret) {
		case -1:
			myerror("Failed to parse directory entry!");
			return -1;
		case 0: // current dir entry and following dir entries are free
			if (llist != NULL) {
				// short dir entry is still missing!
				myerror("ShortDirEntry is missing after LongDirEntries (root directory entry %u)!", j);
				return -1;
			} else {
				return 0;
			}
		case 1: // short dir entry
			parseShortFilename(&de.ShortDirEntry, sname);

			if (OPT_LIST &&
			   strcmp(sname, ".") &&
			   strcmp(sname, "..") &&
			   (((u_char) sname[0]) != DE_FREE) &&
			  !(de.ShortDirEntry.DIR_Atrr & ATTR_VOLUME_ID)) {

				if (!OPT_MORE_INFO) {
					printf("%s\n", (lname[0] != '\0') ? lname : sname);
				} else {
					printf("%s (%s)\n", (lname[0] != '\0') ? lname : "n/a", sname);
				}
			}

			lnde=newDirEntry(sname, lname, &de.ShortDirEntry, llist, entries);
			if (lnde == NULL) {
				myerror("Failed to create DirEntry!");
				return -1;
			}

			if (checkLongDirEntries(lnde)) {
				myerror("checkDirEntry failed at root directory entry %u!", j);
				return -1;
			}

			insertDirEntryList(lnde, list);
			(*direntries)++;
			entries=0;
			llist = NULL;
			lname[0]='\0';
			break;
		case 2: // long dir entry
			if (parseLongFilenamePart(&de.LongDirEntry, tmp, fs->cd)) {
				myerror("Failed to parse long filename part!");
				return -1;
			}

			// insert long dir entry in list
			llist=insertLongDirEntryList(&de.LongDirEntry, llist);
			if (llist == NULL) {
				myerror("Failed to insert LongDirEntry!");
				return -1;
			}

			strncpy(dummy, tmp, MAX_PATH_LEN);
			dummy[MAX_PATH_LEN]='\0';
			strncat(dummy, lname, MAX_PATH_LEN - strlen(dummy));
			dummy[MAX_PATH_LEN]='\0';
			strncpy(lname, dummy, MAX_PATH_LEN);
			dummy[MAX_PATH_LEN]='\0';
			break;
		default:
			myerror("Unhandled return code!");
			return -1;
		}

	}

	if (llist != NULL) {
		// short dir entry is still missing!
		myerror("ShortDirEntry is missing after LongDirEntries (root dir entry %d)!", j);
		return -1;
	}

	return 0;
}

int32_t writeList(struct sFileSystem *fs, struct sDirEntryList *list) {
/*
	writes directory entries to file
*/

	assert(fs != NULL);
	assert(list != NULL);

	struct sLongDirEntryList *tmp;

	// no signal handling while writing (atomic action)
	start_critical_section();

	while(list->next!=NULL) {
		tmp=list->next->ldel;
		while(tmp != NULL) {
			if (fs_write(tmp->lde, DIR_ENTRY_SIZE, 1, fs->fd)<1) {
				// end of critical section
				end_critical_section();

				stderror();
				return -1;
			}
			tmp=tmp->next;
		}
		if (fs_write(list->next->sde, DIR_ENTRY_SIZE, 1, fs->fd)<1) {
			// end of critical section
			end_critical_section();

			stderror();
			return -1;
		}
		list=list->next;
	}

	// sync fs
	syncFileSystem(fs);

	// end of critical section
	end_critical_section();

	return 0;
}

int32_t getClusterChain(struct sFileSystem *fs, u_int32_t startCluster, struct sClusterChain *chain) {
/*
	retrieves an array of all clusters in a cluster chain
	starting with startCluster
*/

	assert(fs != NULL);
	assert(chain != NULL);

	int32_t cluster;
	u_int32_t data,i=0;

	cluster=startCluster;

	switch(fs->FATType) {
	case FATTYPE_FAT12:
		do {
			if (i == fs->maxClusterChainLength) {
				myerror("Cluster chain is too long!");
				return -1;
			}
			if (cluster >= fs->clusters+2) {
				myerror("Cluster %08x does not exist!", data);
				return -1;
			}
			if (insertCluster(chain, cluster) == -1) {
				myerror("Failed to insert cluster!");
				return -1;
			}
			i++;
			if (getFATEntry(fs, cluster, &data)) {
				myerror("Failed to get FAT entry!");
				return -1;
			}
			if (data == 0) {
				myerror("Cluster %08x is marked as unused!", cluster);
				return -1;
			}
			cluster=data;
		} while (cluster < 0x0ff8);	// end of cluster
		break;
	case FATTYPE_FAT16:
		do {
			if (i == fs->maxClusterChainLength) {
				myerror("Cluster chain is too long!");
				return -1;
			}
			if (cluster >= fs->clusters+2) {
				myerror("Cluster %08x does not exist!", data);
				return -1;
			}
			if (insertCluster(chain, cluster) == -1) {
				myerror("Failed to insert cluster!");
				return -1;
			}
			i++;
			if (getFATEntry(fs, cluster, &data)) {
				myerror("Failed to get FAT entry!");
				return -1;
			}
			if (data == 0) {
				myerror("Cluster %08x is marked as unused!", cluster);
				return -1;
			}
			cluster=data;
		} while (cluster < 0xfff8);	// end of cluster
		break;
	case FATTYPE_FAT32:
		do {
			if (i == fs->maxClusterChainLength) {
				myerror("Cluster chain is too long!");
				return -1;
			}
			if ((cluster & 0x0fffffff) >= fs->clusters+2) {
				myerror("Cluster %08x does not exist!", data);
				return -1;
			}
			if (insertCluster(chain, cluster) == -1) {
				myerror("Failed to insert cluster!");
				return -1;
			}
			i++;
			if (getFATEntry(fs, cluster, &data)) {
				myerror("Failed to get FAT entry");
				return -1;
			}
			if ((data & 0x0fffffff) == 0) {
				myerror("Cluster %08x is marked as unused!", cluster);
				return -1;
			}
			cluster=data;
		} while (((cluster & 0x0fffffff) != 0x0ff8fff8) &&
			 ((cluster & 0x0fffffff) < 0x0ffffff8));	// end of cluster
		break;
	case -1:
	default:
		myerror("Failed to get FAT type!");
		return -1;
	}

	return i;
}

int32_t writeClusterChain(struct sFileSystem *fs, struct sDirEntryList *list, struct sClusterChain *chain) {
/*
	writes all entries from list to the cluster chain
*/

	assert(fs != NULL);
	assert(list != NULL);
	assert(chain != NULL);

	u_int32_t i=0, entries=0;
	struct sLongDirEntryList *tmp;
	struct sDirEntryList *p=list->next;
	char empty[DIR_ENTRY_SIZE]={0};

	chain=chain->next;	// we don't need to look at the head element

	if (fs_seek(fs->fd, getClusterOffset(fs, chain->cluster), SEEK_SET)==-1) {
		myerror("Seek error!");
		return -1;
	}

	// no signal handling while writing (atomic action)
	start_critical_section();

	while(p != NULL) {
		if (entries+p->entries <= fs->maxDirEntriesPerCluster) {
			tmp=p->ldel;
			for (i=1;i<p->entries;i++) {
				if (fs_write(tmp->lde, DIR_ENTRY_SIZE, 1, fs->fd)<1) {
					// end of critical section
					end_critical_section();

					stderror();
					return -1;
				}
				tmp=tmp->next;
			}
			if (fs_write(p->sde, DIR_ENTRY_SIZE, 1, fs->fd)<1) {
				// end of critical section
				end_critical_section();

				stderror();
				return -1;
			}
			entries+=p->entries;
		} else {
			tmp=p->ldel;
			for (i=1;i<=fs->maxDirEntriesPerCluster-entries;i++) {
				if (fs_write(tmp->lde, DIR_ENTRY_SIZE, 1, fs->fd)<1) {
					// end of critical section
					end_critical_section();

					stderror();
					return -1;
				}
				tmp=tmp->next;
			}
			chain=chain->next; entries=p->entries - (fs->maxDirEntriesPerCluster - entries);	// next cluster
			if (fs_seek(fs->fd, getClusterOffset(fs, chain->cluster), SEEK_SET)==-1) {

				// end of critical section
				end_critical_section();

				myerror("Seek error!");
				return -1;
			}
			while(tmp!=NULL) {
				if (fs_write(tmp->lde, DIR_ENTRY_SIZE, 1, fs->fd)<1) {
					// end of critical section
					end_critical_section();

					stderror();
					return -1;
				}
				tmp=tmp->next;
			}
			if (fs_write(p->sde, DIR_ENTRY_SIZE, 1, fs->fd)<1) {
				// end of critical section
				end_critical_section();

				stderror();
				return -1;
			}
		}
		p=p->next;
	}
	if (entries < fs->maxDirEntriesPerCluster) {
		if (fs_write(empty, DIR_ENTRY_SIZE, 1, fs->fd)<1) {
			// end of critical section
			end_critical_section();

			stderror();
			return -1;
		}
	}

	// sync fs
	syncFileSystem(fs);

	// end of critical section
	end_critical_section();

	return 0;

}

int32_t sortSubdirectories(struct sFileSystem *fs, struct sDirEntryList *list, const char (*path)[MAX_PATH_LEN+1]) {
/*
	sorts sub directories in a FAT file system
*/
	assert(fs != NULL);
	assert(list != NULL);
	assert(path != NULL);

	struct sDirEntryList *p;
	char newpath[MAX_PATH_LEN+1]={0};
	u_int32_t c, value;

	// sort sub directories
	p=list->next;
	while (p != NULL) {
		if ((p->sde->DIR_Atrr & ATTR_DIRECTORY) &&
			((u_char) p->sde->DIR_Name[0] != DE_FREE) &&
			!(p->sde->DIR_Atrr & ATTR_VOLUME_ID) &&
			(strcmp(p->sname, ".")) && strcmp(p->sname, "..")) {

			c=(SwapInt16(p->sde->DIR_FstClusHI) * 65536 + SwapInt16(p->sde->DIR_FstClusLO));
			if (getFATEntry(fs, c, &value) == -1) {
				myerror("Failed to get FAT entry!");
				return -1;
			}

			strncpy(newpath, (char*) path, MAX_PATH_LEN - strlen(newpath));
			newpath[MAX_PATH_LEN]='\0';
			if ((p->lname != NULL) && (p->lname[0] != '\0')) {
				strncat(newpath, p->lname, MAX_PATH_LEN - strlen(newpath));
				newpath[MAX_PATH_LEN]='\0';
				strncat(newpath, "/", MAX_PATH_LEN - strlen(newpath));
				newpath[MAX_PATH_LEN]='\0';
			} else {
				strncat(newpath, p->sname, MAX_PATH_LEN - strlen(newpath));
				newpath[MAX_PATH_LEN]='\0';
				strncat(newpath, "/", MAX_PATH_LEN - strlen(newpath));
				newpath[MAX_PATH_LEN]='\0';
			}

			if (sortClusterChain(fs, c, (const char(*)[MAX_PATH_LEN+1]) newpath) == -1) {
				myerror("Failed to sort cluster chain!");
				return -1;
			}

		}
		p=p->next;
	}

	return 0;
}

int32_t sortClusterChain(struct sFileSystem *fs, u_int32_t cluster, const char (*path)[MAX_PATH_LEN+1]) {
/*
	sorts directory entries in a cluster
*/

	assert(fs != NULL);
	assert(path != NULL);

	u_int32_t direntries;
	int32_t clen;
	struct sClusterChain *ClusterChain;
	struct sDirEntryList *list;

	u_int32_t match;

	if (!OPT_REGEX) {
		match=matchesDirPathLists(OPT_INCL_DIRS, OPT_INCL_DIRS_REC, OPT_EXCL_DIRS, OPT_EXCL_DIRS_REC, path);
	} else {
		match=!matchesRegExList(OPT_REGEX_EXCL, (const char *) path);
		if (OPT_REGEX_INCL->next != NULL) match &= matchesRegExList(OPT_REGEX_INCL, (const char *) path);
	}

	if ((ClusterChain=newClusterChain()) == NULL) {
		myerror("Failed to generate new ClusterChain!");
		return -1;
	}

	if ((list = newDirEntryList()) == NULL) {
		myerror("Failed to generate new dirEntryList!");
		freeClusterChain(ClusterChain);
		return -1;
	}

	if ((clen=getClusterChain(fs, cluster, ClusterChain)) == -1 ) {
		myerror("Failed to get cluster chain!");
		freeDirEntryList(list);
		freeClusterChain(ClusterChain);
		return -1;
	}

	if (!OPT_LIST) {
		if (match) {
			infomsg("Sorting directory %s\n", path);
			if (OPT_MORE_INFO)
				infomsg("Start cluster: %08lx, length: %d (%d bytes)\n",
					cluster, clen, clen*fs->clusterSize);
		}
	} else {
		printf("%s\n", (char*) path);
		if (OPT_MORE_INFO)
			infomsg("Start cluster: %08lx, length: %d (%d bytes)\n",
				cluster, clen, clen*fs->clusterSize);
	}

	if (parseClusterChain(fs, ClusterChain, list, &direntries) == -1) {
		myerror("Failed to parse cluster chain!");
		freeDirEntryList(list);
		freeClusterChain(ClusterChain);
		return -1;
	}

	if (!OPT_LIST) {
		// sort directory if selected
		if (match) {

			if (OPT_RANDOM) randomizeDirEntryList(list, direntries);

			if (writeClusterChain(fs, list, ClusterChain) == -1) {
				myerror("Failed to write cluster chain!");
				freeDirEntryList(list);
				freeClusterChain(ClusterChain);
				return -1;
			}
		}
	} else {
		printf("\n");
	}

	freeClusterChain(ClusterChain);

	// sort subdirectories
	if (sortSubdirectories(fs, list, path) == -1 ){
		myerror("Failed to sort subdirectories!");
		return -1;
	}

	freeDirEntryList(list);

	return 0;
}

int32_t sortFAT1xRootDirectory(struct sFileSystem *fs) {
/*
	sorts the root directory of a FAT12 or FAT16 file system
*/

	assert(fs != NULL);

	off_t BSOffset;

	u_int32_t direntries=0;

	struct sDirEntryList *list;

	u_int32_t match;

	if (!OPT_REGEX) {
		match=matchesDirPathLists(OPT_INCL_DIRS,
					OPT_INCL_DIRS_REC,
					OPT_EXCL_DIRS,
					OPT_EXCL_DIRS_REC,
					(const char(*)[MAX_PATH_LEN+1]) "/");
	} else {
		match=!matchesRegExList(OPT_REGEX_EXCL, (const char *) "/");
		if (OPT_REGEX_INCL->next != NULL) match &= matchesRegExList(OPT_REGEX_INCL, (const char *) "/");
	}

	if (!OPT_LIST) {
		if (match) {
			infomsg("Sorting directory /\n");
		}
	} else {
		printf("/\n");
	}

	if ((list = newDirEntryList()) == NULL) {
		myerror("Failed to generate new dirEntryList!");
		return -1;
	}

	if (parseFAT1xRootDirEntries(fs, list, &direntries) == -1) {
		myerror("Failed to parse root directory entries!");
		return -1;
	}

	if (!OPT_LIST) {

		// sort matching directories
		if (match) {

			if (OPT_RANDOM) randomizeDirEntryList(list, direntries);

			BSOffset = ((off_t)SwapInt16(fs->bs.BS_RsvdSecCnt) +
				fs->bs.BS_NumFATs * fs->FATSize) * fs->sectorSize;
			fs_seek(fs->fd, BSOffset, SEEK_SET);

			// write the sorted entries back to the fs
			if (writeList(fs, list) == -1) {
				freeDirEntryList(list);
			  	myerror("Failed to write root directory entries!");
				return -1;
			}
		}

	} else {
		printf("\n");
	}

	// sort subdirectories
	if (sortSubdirectories(fs, list, (const char (*)[MAX_PATH_LEN+1]) "/") == -1 ){
		myerror("Failed to sort subdirectories!");
		freeDirEntryList(list);
		return -1;
	}

	freeDirEntryList(list);

	return 0;
}

int32_t sortFileSystem(char *filename) {
/*
	sort FAT file system
*/

	assert(filename != NULL);

	u_int32_t mode = FS_MODE_RW;

	struct sFileSystem fs = {0};

	if (!OPT_FORCE && OPT_LIST) {
		mode = FS_MODE_RO_EXCL;
	} else if (!OPT_FORCE && !OPT_LIST) {
		mode = FS_MODE_RW_EXCL;
	} else if (OPT_FORCE && OPT_LIST) {
		mode = FS_MODE_RO;
	}

	if (openFileSystem(filename, mode, &fs)) {
		myerror("Failed to open file system!");
		return -1;
	}

	if (checkFATs(&fs)) {
		myerror("FATs don't match! Please repair file system!");
		closeFileSystem(&fs);
		return -1;
	}

	switch(fs.FATType) {
	case FATTYPE_FAT12:
		// FAT12
		// root directory has fixed size and position
		infomsg("File system: FAT12.\n\n");
		if (sortFAT1xRootDirectory(&fs) == -1) {
			myerror("Failed to sort FAT12 root directory!");
			closeFileSystem(&fs);
			return -1;
		}
		break;
	case FATTYPE_FAT16:
		// FAT16
		// root directory has fixed size and position
		infomsg("File system: FAT16.\n\n");
		if (sortFAT1xRootDirectory(&fs) == -1) {
			myerror("Failed to sort FAT16 root directory!");
			closeFileSystem(&fs);
			return -1;
		}
		break;
	case FATTYPE_FAT32:
		// FAT32
		// root directory lies in cluster chain,
		// so sort it like all other directories
		infomsg("File system: FAT32.\n\n");
		if (sortClusterChain(&fs, SwapInt32(fs.bs.FATxx.FAT32.BS_RootClus), (const char(*)[MAX_PATH_LEN+1]) "/") == -1) {
			myerror("Failed to sort first cluster chain!");
			closeFileSystem(&fs);
			return -1;
		}
		break;
	default:
		myerror("Failed to get FAT type!");
		closeFileSystem(&fs);
		return -1;
	}

	closeFileSystem(&fs);

	return 0;
}
