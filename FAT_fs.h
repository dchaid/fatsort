/*
	FATDefrag, utility for defragmentation of FAT file systems
	Copyright (C) 2013 Boris Leidner <fatdefrag(at)formenos.de>

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
	This file contains/describes functions that are used to read, write, check,
	and use FAT filesystems.
*/

#ifndef __FAT_fs_h__
#define __FAT_fs_h__

// FS open mode bits
#define FS_MODE_RO 1
#define FS_MODE_RO_EXCL 2
#define FS_MODE_RW 3
#define FS_MODE_RW_EXCL 4

// FAT types
#define FATTYPE_FAT12 12
#define FATTYPE_FAT16 16
#define FATTYPE_FAT32 32

// file attributes
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define ATTR_LONG_NAME_MASK (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_DIRECTORY | ATTR_ARCHIVE)

// constants for the LDIR structure
#define DE_FREE 0xe5
#define DE_FOLLOWING_FREE 0x00
#define LAST_LONG_ENTRY 0x40

#define DIR_ENTRY_SIZE 32

// maximum path len on FAT file systems (above specification)
#define MAX_PATH_LEN 512

// maximum file len
// (specification: file < 4GB which is 
// maximum clusters in chain * cluster size)
#define MAX_FILE_LEN 0xFFFFFFFF
#define MAX_DIR_ENTRIES 65536
#define MAX_CLUSTER_SIZE 65536

#include <stdio.h>
#include <sys/types.h>
#include <iconv.h>

#include "platform.h"

// Directory entry structures
// Structure for long directory names
struct sLongDirEntry {
	u_char LDIR_Ord;		// Order of entry in sequence
	char LDIR_Name1[10];		// Chars 1-5 of long name
	u_char LDIR_Attr;		// Attributes (ATTR_LONG_NAME must be set)
	u_char LDIR_Type;		// Type
	u_char LDIR_Checksum;		// Short name checksum
	char LDIR_Name2[12];		// Chars 6-11 of long name
	u_int16_t LDIR_FstClusLO;	// Zero
	char LDIR_Name3[4];		// Chars 12-13 of long name
} __attribute__((packed));

// Structure for old short directory names
struct sShortDirEntry {
	char DIR_Name[11];		// Short name
	u_char DIR_Atrr;		// File attributes
	u_char DIR_NTRes;		// Reserved for NT
	u_char DIR_CrtTimeTenth;	// Time of creation in ms
	u_int16_t DIR_CrtTime;		// Time of creation
	u_int16_t DIR_CrtDate;		// Date of creation
	u_int16_t DIR_LstAccDate;	// Last access date
	u_int16_t DIR_FstClusHI;	// Hiword of first cluster
	u_int16_t DIR_WrtTime;		// Time of last write
	u_int16_t DIR_WrtDate;		// Date of last write
	u_int16_t DIR_FstClusLO;	// Loword of first cluster
	u_int32_t DIR_FileSize;		// file size in bytes
} __attribute__((packed));

union sDirEntry {
	struct sShortDirEntry ShortDirEntry;
	struct sLongDirEntry LongDirEntry;
} __attribute__((packed));

// Bootsector structures
// FAT12 and FAT16
struct sFAT12_16 {
	u_char BS_DrvNum;		// Physical drive number
	u_char BS_Reserved;		// Current head
	u_char BS_BootSig;		// Signature
	u_int32_t BS_VolID;		// Volume ID
	char BS_VolLab[11];		// Volume Label
	char BS_FilSysType[8];		// FAT file system type (e.g. FAT, FAT12, FAT16, FAT32)
	u_char unused[448];		// unused space in bootsector
} __attribute__((packed));

// FAT32
struct sFAT32 {
	u_int32_t BS_FATSz32;		// Sectors per FAT
	u_int16_t BS_ExtFlags;		// Flags
	u_int16_t BS_FSVer;		// Version
	u_int32_t BS_RootClus;		// Root Directory Cluster
	u_int16_t BS_FSInfo;		// Sector of FSInfo structure
	u_int16_t BS_BkBootSec;		// Sector number of the boot sector copy in reserved sectors
	char BS_Reserved[12];		// for future expansion
	char BS_DrvNum;			// see fat12/16
	char BS_Reserved1;		// see fat12/16
	char BS_BootSig;		// ...
	u_int32_t BS_VolID;
	char BS_VolLab[11];
	char BS_FilSysType[8];
	u_char unused[420];		// unused space in bootsector
} __attribute__((packed));

union sFATxx {
	struct sFAT12_16 FAT12_16;
	struct sFAT32 FAT32;
} __attribute__((packed));

// First sector = boot sector
struct sBootSector {
	u_char BS_JmpBoot[3];		// Jump instruction (to skip over header on boot)
	char BS_OEMName[8];		// OEM Name (padded with spaces)
	u_int16_t BS_BytesPerSec;	// Bytes per sector
	u_char BS_SecPerClus;		// Sectors per cluster
	u_int16_t BS_RsvdSecCnt;	// Reserved sector count (including boot sector)
	u_char BS_NumFATs;		// Number of file allocation tables
	u_int16_t BS_RootEntCnt;	// Number of root directory entries
	u_int16_t BS_TotSec16;		// Total sectors (bits 0-15)
	u_char BS_Media;		// Media descriptor
	u_int16_t BS_FATSz16;		// Sectors per file allocation table
	u_int16_t BS_SecPerTrk;		// Sectors per track
	u_int16_t BS_NumHeads;		// Number of heads
	u_int32_t BS_HiddSec;		// Hidden sectors
	u_int32_t BS_TotSec32;		// Total sectors (bits 16-47)
	union sFATxx FATxx;
	u_int16_t BS_EndOfBS;		// marks end of bootsector
} __attribute__((packed));

// FAT32 FSInfo structure
struct sFSInfo {
	u_int32_t FSI_LeadSig;
	u_int8_t FSI_Reserved1[480];
	u_int32_t FSI_StrucSig;
	u_int32_t FSI_Free_Count;
	u_int32_t FSI_Nxt_Free;
	u_int8_t FSI_Reserved2[12];
	u_int32_t FSI_TrailSig;
} __attribute__((packed));

// holds information about the file system
struct sFileSystem {
	FILE *fd;
	int32_t rfd;
	u_int32_t mode;
	char path[MAX_PATH_LEN+1];
	struct sBootSector bs;
	int32_t FATType;
	u_int32_t clusterCount;
	u_int16_t sectorSize;
	u_int32_t totalSectors;
	u_int32_t clusterSize;
	int32_t clusters;
	u_int32_t FATSize;
	u_int64_t FSSize;
	u_int32_t maxDirEntriesPerCluster;
	u_int32_t maxClusterChainLength;
	u_int32_t firstDataSector;
	iconv_t cd;
};

// functions

// opens file system and calculates file system information
int32_t openFileSystem(char *path, u_int32_t mode, struct sFileSystem *fs);

// update boot sector
int32_t writeBootSector(struct sFileSystem *fs);

// sync file system
int32_t syncFileSystem(struct sFileSystem *fs);

// closes file system
int32_t closeFileSystem(struct sFileSystem *fs);

// lazy check if this is really a FAT bootsector
int32_t check_bootsector(struct sBootSector *bs);

// retrieves FAT entry for a cluster number
int32_t getFATEntry(struct sFileSystem *fs, u_int32_t cluster, u_int32_t *data);

// read FAT from file system
void *readFAT(struct sFileSystem *fs, u_int16_t nr);

// write FAT to file system
int32_t writeFAT(struct sFileSystem *fs, void *fat);

// read cluster from file systen
void *readCluster(struct sFileSystem *fs, u_int32_t cluster);

// write cluster to file systen
int32_t writeCluster(struct sFileSystem *fs, u_int32_t cluster, void *data);

// checks whether data marks a free cluster
u_int16_t isFreeCluster(const u_int32_t data);

// checks whether data marks the end of a cluster chain
u_int16_t isEOC(struct sFileSystem *fs, const u_int32_t data);

// checks whether data marks a bad cluster
u_int16_t isBadCluster(struct sFileSystem *fs, const u_int32_t data);

// returns the offset of a specific cluster in the data region of the file system
off_t getClusterOffset(struct sFileSystem *fs, u_int32_t cluster);

// parses one directory entry
int32_t parseEntry(struct sFileSystem *fs, union sDirEntry *de);

// calculate checksum for short dir entry name
u_char calculateChecksum (char *sname);

// checks whether all FATs have the same content
int32_t checkFATs(struct sFileSystem *fs);

// reads FSInfo structure
int32_t readFSInfo(struct sFileSystem *fs, struct sFSInfo *fsInfo);

// write FSInfo structure
int32_t writeFSInfo(struct sFileSystem *fs, struct sFSInfo *fsInfo);

#endif // __FAT_fs_h__
