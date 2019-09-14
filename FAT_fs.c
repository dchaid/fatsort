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

#include "FAT_fs.h"

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/param.h>
#include <iconv.h>

#include "errors.h"
#include "endianness.h"
#include "fileio.h"
#include "mallocv.h"

// used to check if device is mounted
#if defined(__LINUX__)
#include <mntent.h>
#elif defined (__BSD__)
#include <sys/ucred.h>
#include <sys/mount.h>
#endif

int32_t check_mounted(char *filename) {
/*
	check if filesystem is already mounted
*/

#if defined(__LINUX__)
	FILE *fd;
	struct mntent *mnt;
	int32_t ret = 0;
	char rp_filename[MAXPATHLEN+1], rp_mnt_fsname[MAXPATHLEN+1];
	
	if ((fd = setmntent("/etc/mtab", "r")) == NULL) {
		stderror();
		return -1;
	}

	// get real path
	if (realpath(filename, rp_filename) == NULL) {
		myerror("Unable to get realpath of filename!");
		return -1;
	}
	
	while ((mnt = getmntent(fd)) != NULL) {
		if (realpath(mnt->mnt_fsname, rp_mnt_fsname) != NULL) {
			if (strcmp(rp_mnt_fsname, rp_filename) == 0) {
				ret = 1;
				break;
			}
		}
	}
	
	if (endmntent(fd) != 1) {
		myerror("Closing mtab failed!");
		return -1;
	}

	return ret;

#elif defined(__BSD__)
	struct statfs *mntbuf;
	int i, mntsize;
	int32_t ret = 0;
	char rp_filename[MAXPATHLEN], rp_mnt_fsname[MAXPATHLEN+1];

	// get real path
	if (realpath(filename, rp_filename) == NULL) {
		myerror("Unable to get realpath of filename!");
		return -1;
	}

	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);

	if (mntsize == 0) {
		stderror();
		return -1;
	}
	
	for (i = mntsize - 1; i >= 0; i--) {
		realpath(mntbuf[i].f_mntfromname, rp_mnt_fsname);
		if (strcmp(rp_mnt_fsname, rp_filename) == 0) {
			ret = 1;
			break;
		}
	}
	
	return ret;
#else	
	// ok, we don't know how to check this on an unknown platform
	myerror("Don't know how to check whether filesystem is mounted! Use option '-f' to sort nonetheless.");

	return -1;
#endif
}

int32_t check_bootsector(struct sBootSector *bs) {
/*
	lazy check if this is really a FAT bootsector
*/

	assert(bs != NULL);

	if (!((bs->BS_JmpBoot[0] == 0xeb) &&
	     (bs->BS_JmpBoot[2] == 0x90)) &&
		!(bs->BS_JmpBoot[0] == 0xe9)) {
		// boot sector does not begin with specific instruction
 		myerror("Boot sector does not begin with jump instruction!");
		return -1;
	} else if (SwapInt16(bs->BS_EndOfBS) != 0xaa55) {
		// end of bootsector marker is missing
		myerror("End of boot sector marker is missing!");
		return -1;
	} else if (SwapInt16(bs->BS_BytesPerSec) == 0) {
		myerror("Sectors have a size of zero!");
		return -1;
	} else if (SwapInt16(bs->BS_BytesPerSec) % 512 != 0) {
		myerror("Sector size is not a multiple of 512 (%u)!", SwapInt16(bs->BS_BytesPerSec));
		return -1;
	} else if (bs->BS_SecPerClus == 0) {
		myerror("Cluster size is zero!");
		return -1;
	} else if (bs->BS_SecPerClus * SwapInt16(bs->BS_BytesPerSec) > MAX_CLUSTER_SIZE) {
		myerror("Cluster size is larger than %u kB!", MAX_CLUSTER_SIZE / 1024);
		return -1;
	} else if (SwapInt16(bs->BS_RsvdSecCnt) == 0) {
		myerror("Reserved sector count is zero!");
		return -1;
	} else if (bs->BS_NumFATs == 0) {
		myerror("Number of FATs is zero!");
		return -1;
	} else if (SwapInt16(bs->BS_RootEntCnt) % DIR_ENTRY_SIZE != 0) {
		myerror("Count of root directory entries must be zero or a multiple or 32! (%u)", SwapInt16(bs->BS_RootEntCnt));
		return -1;
	}

	return 0;
}

int32_t read_bootsector(FILE *fd, struct sBootSector *bs) {
/*
	reads bootsector
*/

	assert(fd != NULL);
	assert(bs != NULL);

	// seek to beginning of fs
	if (fs_seek(fd, 0, SEEK_SET) == -1) {
		stderror();
		return -1;
	}

	if (fs_read(bs, sizeof(struct sBootSector), 1, fd) < 1) {
		if (feof(fd)) {
			myerror("Boot sector is too short!");
		} else {
			myerror("Failed to read from file!");
		}
		return -1;
	}

	if (check_bootsector(bs)) {
		myerror("This is not a FAT boot sector or sector is damaged!");
		return -1;
	}

	return 0;
}

int32_t writeBootSector(struct sFileSystem *fs) {
/*
	write boot sector
*/

	// seek to beginning of fs
	if (fs_seek(fs->fd, 0, SEEK_SET) == -1) {
		stderror();
		return -1;
	}

	// write boot sector
	if (fs_write(&(fs->bs), sizeof(struct sBootSector), 1, fs->fd) < 1) {
		stderror();
		return -1;
	}

	//  update backup boot sector for FAT32 file systems
	if (fs->FATType == FATTYPE_FAT32) {
		// seek to beginning of backup boot sector
		if (fs_seek(fs->fd, SwapInt16(fs->bs.FATxx.FAT32.BS_BkBootSec) * fs->sectorSize, SEEK_SET) == -1) {
			stderror();
			return -1;
		}

		// write backup boot sector
		if (fs_write(&(fs->bs), sizeof(struct sBootSector), 1, fs->fd) < 1) {
			stderror();
			return -1;
		}
	}

	return 0;
}

int32_t readFSInfo(struct sFileSystem *fs, struct sFSInfo *fsInfo) {
/*
	reads FSInfo structure
*/

	assert(fs != NULL);
	assert(fsInfo != NULL);

	// seek to beginning of FSInfo structure
	if (fs_seek(fs->fd, SwapInt16(fs->bs.FATxx.FAT32.BS_FSInfo) * fs->sectorSize, SEEK_SET) == -1) {
		stderror();
		return -1;
	}

	if (fs_read(fsInfo, sizeof(struct sFSInfo), 1, fs->fd) < 1) {
		stderror();
		return -1;
	}

	return 0;
}

int32_t writeFSInfo(struct sFileSystem *fs, struct sFSInfo *fsInfo) {
/*
	write FSInfo structure
*/
	assert(fs != NULL);
	assert(fsInfo != NULL);

	// seek to beginning of FSInfo structure
	if (fs_seek(fs->fd, SwapInt16(fs->bs.FATxx.FAT32.BS_FSInfo) * fs->sectorSize, SEEK_SET) == -1) {
		stderror();
		return -1;
	}

	// write boot sector
	if (fs_write(fsInfo, sizeof(struct sFSInfo), 1, fs->fd) < 1) {
		stderror();
		return -1;
	}

	return 0;
}

int32_t getCountOfClusters(struct sBootSector *bs) {
/*
	calculates count of clusters
*/

	assert(bs != NULL);

	u_int32_t RootDirSectors, FATSz, TotSec, DataSec;
	int32_t retvalue;

	RootDirSectors = ((SwapInt16(bs->BS_RootEntCnt) * DIR_ENTRY_SIZE) + (SwapInt16(bs->BS_BytesPerSec) - 1));
	RootDirSectors = RootDirSectors / SwapInt16(bs->BS_BytesPerSec);

	if (bs->BS_FATSz16 != 0) {
		FATSz = SwapInt16(bs->BS_FATSz16);
	} else {
		FATSz = SwapInt32(bs->FATxx.FAT32.BS_FATSz32);
	}
	if (SwapInt16(bs->BS_TotSec16) != 0) {
		TotSec = SwapInt16(bs->BS_TotSec16);
	} else {
		TotSec = SwapInt32(bs->BS_TotSec32);
	}
	DataSec = TotSec - (SwapInt16(bs->BS_RsvdSecCnt) + (bs->BS_NumFATs * FATSz) + RootDirSectors);

	retvalue = DataSec / bs->BS_SecPerClus;
	if (retvalue <= 0) {
		myerror("Failed to calculate count of clusters!");
		return -1;
	}
	return retvalue;
}

int32_t getFATType(struct sBootSector *bs) {
/*
	retrieves FAT type from bootsector
*/

	assert(bs != NULL);

	int32_t CountOfClusters;

	CountOfClusters=getCountOfClusters(bs);
	if (CountOfClusters == -1) {
		myerror("Failed to get count of clusters!");
		return -1;
	} else if (CountOfClusters < 4096) { // FAT12!
		return FATTYPE_FAT12;
	} else if (CountOfClusters < 65525) { // FAT16!
		return FATTYPE_FAT16;
	} else { // FAT32!
		return FATTYPE_FAT32;
	}
}

u_int16_t isFreeCluster(const u_int32_t data) {
/*	
	checks whether data marks a free cluster
*/

	    return (data & 0x0FFFFFFF) == 0;
}

u_int16_t isEOC(struct sFileSystem *fs, const u_int32_t data) {
/*	
	checks whether data marks the end of a cluster chain
*/

	assert(fs != NULL);

	if(fs->FATType == FATTYPE_FAT12) {
	    if(data >= 0x0FF8)
		return 1;
	} else if(fs->FATType == FATTYPE_FAT16) {
	    if(data >= 0xFFF8)
		return 1;
	} else if (fs->FATType == FATTYPE_FAT32) {
	    if((data & 0x0FFFFFFF) >= 0x0FFFFFF8)
		return 1;
	}

	return 0;
}

u_int16_t isBadCluster(struct sFileSystem *fs, const u_int32_t data) {
/*
	checks whether data marks a bad cluster
*/
	assert(fs != NULL);

	if(fs->FATType == FATTYPE_FAT12) {
	    if(data == 0xFF7)
		return 1;
	} else if(fs->FATType == FATTYPE_FAT16) {
	    if(data == 0xFFF7)
		return 1;
	} else if (fs->FATType == FATTYPE_FAT32) {
	    if ((data & 0x0FFFFFFF) == 0x0FFFFFF7)
		return 1;
	}

	return 0;
}


void *readFAT(struct sFileSystem *fs, u_int16_t nr) {
/*
	reads a FAT from file system fs
*/

	assert(fs != NULL);
	assert(nr < fs->bs.BS_NumFATs);

	u_int32_t FATSizeInBytes;
	off_t BSOffset;

	void *FAT;

	FATSizeInBytes = fs->FATSize * fs->sectorSize;

	if ((FAT=malloc(FATSizeInBytes))==NULL) {
		stderror();
		return NULL;
	}
	BSOffset = (off_t)SwapInt16(fs->bs.BS_RsvdSecCnt) * SwapInt16(fs->bs.BS_BytesPerSec);
	if (fs_seek(fs->fd, BSOffset + nr * FATSizeInBytes, SEEK_SET) == -1) {
		myerror("Seek error!");
		free(FAT);
		return NULL;
	}
	if (fs_read(FAT, FATSizeInBytes, 1, fs->fd) < 1) {
		myerror("Failed to read from file!");
		free(FAT);
		return NULL;
	}

	return FAT;

}

int32_t writeFAT(struct sFileSystem *fs, void *fat) {
/*
	write FAT to file system
*/

	assert(fs != NULL);
	assert(fat != NULL);

	u_int32_t FATSizeInBytes, nr;
	off_t BSOffset;

	FATSizeInBytes = fs->FATSize * fs->sectorSize;

	BSOffset = (off_t)SwapInt16(fs->bs.BS_RsvdSecCnt) * SwapInt16(fs->bs.BS_BytesPerSec);

	// write all FATs!
	for(nr=0; nr< fs->bs.BS_NumFATs; nr++) {
		if (fs_seek(fs->fd, BSOffset + nr * FATSizeInBytes, SEEK_SET) == -1) {
			myerror("Seek error!");
			return -1;
		}
		if (fs_write(fat, FATSizeInBytes, 1, fs->fd) < 1) {
			myerror("Failed to read from file!");
			return -1;
		}
	}

	return 0;
}

int32_t checkFATs(struct sFileSystem *fs) {
/*
	checks whether all FATs have the same content
*/

	assert(fs != NULL);

	u_int32_t FATSizeInBytes;
	int32_t result=0;
	int32_t i;	

	off_t BSOffset;

	char *FAT1, *FATx;

	// if there is just one FAT, we don't have to check anything
	if (fs->bs.BS_NumFATs < 2) {
		return 0;
	}

	FATSizeInBytes = fs->FATSize * fs->sectorSize;

	if ((FAT1=malloc(FATSizeInBytes))==NULL) {
		stderror();
		return -1;
	}
	if ((FATx=malloc(FATSizeInBytes))==NULL) {
		stderror();
		free(FAT1);
		return -1;
	}
	BSOffset = (off_t)SwapInt16(fs->bs.BS_RsvdSecCnt) * SwapInt16(fs->bs.BS_BytesPerSec);
	if (fs_seek(fs->fd, BSOffset, SEEK_SET) == -1) {
		myerror("Seek error!");
		free(FAT1);
		free(FATx);
		return -1;
	}
	if (fs_read(FAT1, FATSizeInBytes, 1, fs->fd) < 1) {
		myerror("Failed to read from file!");
		free(FAT1);
		free(FATx);
		return -1;
	}

	for(i=1; i < fs->bs.BS_NumFATs; i++) {
		if (fs_seek(fs->fd, BSOffset+FATSizeInBytes, SEEK_SET) == -1) {
			myerror("Seek error!");
			free(FAT1);
			free(FATx);
			return -1;
		}
		if (fs_read(FATx, FATSizeInBytes, 1, fs->fd) < 1) {
			myerror("Failed to read from file!");
			free(FAT1);
			free(FATx);
			return -1;
		}

		//printf("FAT1: %08lx FATx: %08lx\n", FAT1[0], FATx[0]);

		result = memcmp(FAT1, FATx, FATSizeInBytes) != 0;
		if (result) break; // FATs don't match

	}

	free(FAT1);
	free(FATx);

	return result;
}

int32_t getFATEntry(struct sFileSystem *fs, u_int32_t cluster, u_int32_t *data) {
/*
	retrieves FAT entry for a cluster number
*/

	assert(fs != NULL);
	assert(data != NULL);

	off_t FATOffset, BSOffset;

	*data=0;

	switch(fs->FATType) {
	case FATTYPE_FAT32:
		FATOffset = (off_t)cluster * 4;
		BSOffset = (off_t)SwapInt16(fs->bs.BS_RsvdSecCnt) * SwapInt16(fs->bs.BS_BytesPerSec) + FATOffset;
		if (fs_seek(fs->fd, BSOffset, SEEK_SET) == -1) {
			myerror("Seek error!");
			return -1;
		}
		if (fs_read(data, 4, 1, fs->fd) < 1) {
			myerror("Failed to read from file!");
			return -1;
		}
		*data=SwapInt32(*data);
		*data = *data & 0x0fffffff;
		break;
	case FATTYPE_FAT16:
		FATOffset = (off_t)cluster * 2;
		BSOffset = (off_t) SwapInt16(fs->bs.BS_RsvdSecCnt) * SwapInt16(fs->bs.BS_BytesPerSec) + FATOffset;
		if (fs_seek(fs->fd, BSOffset, SEEK_SET) == -1) {
			myerror("Seek error!");
			return -1;
		}
		if (fs_read(data, 2, 1, fs->fd)<1) {
			myerror("Failed to read from file!");
			return -1;
		}
		*data=SwapInt32(*data);
		break;
	case FATTYPE_FAT12:
		FATOffset = (off_t) cluster + (cluster / 2);
		BSOffset = (off_t) SwapInt16(fs->bs.BS_RsvdSecCnt) * SwapInt16(fs->bs.BS_BytesPerSec) + FATOffset;
		if (fs_seek(fs->fd, BSOffset, SEEK_SET) == -1) {
			myerror("Seek error!");
			return -1;
		}
		if (fs_read(data, 2, 1, fs->fd)<1) {
			myerror("Failed to read from file!");
			return -1;
		}

		*data=SwapInt32(*data);

		if (cluster & 1)  {
			*data = *data >> 4;	/* cluster number is odd */
		} else {
			*data = *data & 0x0FFF;	/* cluster number is even */
		}
		break;
	default:
		myerror("Failed to get FAT type!");
		return -1;
	}

	return 0;

}

off_t getClusterOffset(struct sFileSystem *fs, u_int32_t cluster) {
/*
	returns the offset of a specific cluster in the
	data region of the file system
*/

	assert(fs != NULL);
	assert(cluster > 1);

	return (((off_t)(cluster - 2) * fs->bs.BS_SecPerClus) + fs->firstDataSector) * fs->sectorSize;

}

void *readCluster(struct sFileSystem *fs, u_int32_t cluster) {
/*
	read cluster from file system
*/
	void *dummy;

	if (fs_seek(fs->fd, getClusterOffset(fs, cluster), SEEK_SET) != 0) {
		stderror();
		return NULL;
	}

	if ((dummy = malloc(fs->clusterSize)) == NULL) {
		stderror();
		return NULL;
	}

	if ((fs_read(dummy, fs->clusterSize, 1, fs->fd)<1)) {
		myerror("Failed to read cluster!");
		return NULL;
	}

	return dummy;
}

int32_t writeCluster(struct sFileSystem *fs, u_int32_t cluster, void *data) {
/*
	write cluster to file systen
*/
	if (fs_seek(fs->fd, getClusterOffset(fs, cluster), SEEK_SET) != 0) {
		stderror();
		return -1;
	}

	if (fs_write(data, fs->clusterSize, 1, fs->fd)<1) {
		stderror();
		return -1;
	}

	return 0;
}

int32_t parseEntry(struct sFileSystem *fs, union sDirEntry *de) {
/*
	parses one directory entry
*/

	assert(fs != NULL);
	assert(de != NULL);

	if ((fs_read(de, DIR_ENTRY_SIZE, 1, fs->fd)<1)) {
		myerror("Failed to read from file!");
		return -1;
	}

	if (de->ShortDirEntry.DIR_Name[0] == DE_FOLLOWING_FREE ) return 0; // no more entries

	// long dir entry
	if ((de->LongDirEntry.LDIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) return 2;

	return 1; // short dir entry
}

u_char calculateChecksum (char *sname) {
	u_char len;
	u_char sum;

	sum = 0;
	for (len=11; len!=0; len--) {
		sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *sname++;
	}
	return sum;
}


int32_t openFileSystem(char *path, u_int32_t mode, struct sFileSystem *fs) {
/*
	opens file system and assemlbes file system information into data structure
*/
	assert(path != NULL);
	assert(fs != NULL);

	int32_t ret;

	fs->rfd=0;

	switch(mode) {
		case FS_MODE_RO:
			if ((fs->fd=fopen(path, "rb")) == NULL) {
				stderror();
				return -1;
			}			
			break;
		case FS_MODE_RW:
			if ((fs->fd=fopen(path, "r+b")) == NULL) {
				stderror();
				return -1;
			}
			break;
		case FS_MODE_RO_EXCL:
		case FS_MODE_RW_EXCL:
			// this check is only done for user convenience
			// open would fail too if device is mounted, but without specific error message
			ret=check_mounted(path);
			switch (ret) {
				case 0: break;  // filesystem not mounted
				case 1:		// filesystem mounted
					myerror("Filesystem is mounted!");
					return -1;
				case -1:	// unable to check
				default:
					myerror("Could not check whether filesystem is mounted!");
					return -1;
			}

			// opens the device exclusively. This is not mandatory! e.g. mkfs.vfat ignores it!
			if ((fs->rfd=open(path, (mode == FS_MODE_RO_EXCL) ? O_RDONLY | O_EXCL : O_RDWR | O_EXCL)) == -1) {
				stderror();
				return -1;
			}

			// connect the file descriptor to a stream
			if ((fs->fd=fdopen(fs->rfd, (mode == FS_MODE_RO_EXCL) ? "rb" : "r+b")) == NULL) {
				stderror();
				close(fs->rfd);
				return -1;
			}
			break;
		default:
			myerror("Mode not supported!");
			return -1;
	}

	// read boot sector
	if (read_bootsector(fs->fd, &(fs->bs))) {
		myerror("Failed to read boot sector!");
		fs_close(fs->fd);
		close(fs->rfd);
		return -1;
	}

	strncpy(fs->path, path, MAX_PATH_LEN);
	fs->path[MAX_PATH_LEN] = '\0';


	if (SwapInt16(fs->bs.BS_TotSec16) != 0) {
		fs->totalSectors = SwapInt16(fs->bs.BS_TotSec16);
	} else {
		fs->totalSectors = SwapInt32(fs->bs.BS_TotSec32);
	}

	if (fs->totalSectors == 0) {
		myerror("Count of total sectors must not be zero!");
		fs_close(fs->fd);
		close(fs->rfd);
		return -1;
	}

	fs->FATType = getFATType(&(fs->bs));
	if (fs->FATType == -1) {
		myerror("Failed to get FAT type!");
		fs_close(fs->fd);
		close(fs->rfd);
		return -1;
	}

	if ((fs->FATType == FATTYPE_FAT32) && (fs->bs.FATxx.FAT32.BS_FATSz32 == 0)) {
		myerror("32-bit count of FAT sectors must not be zero for FAT32!");
		fs_close(fs->fd);
		close(fs->rfd);
		return -1;
	} else 	if (((fs->FATType == FATTYPE_FAT12) || (fs->FATType == FATTYPE_FAT16)) && (fs->bs.BS_FATSz16 == 0)) {
		myerror("16-bit count of FAT sectors must not be zero for FAT1x!");
		fs_close(fs->fd);
		close(fs->rfd);
		return -1;
	}	

	if (fs->bs.BS_FATSz16 != 0) {
		fs->FATSize = SwapInt16(fs->bs.BS_FATSz16);
	} else {
		fs->FATSize = SwapInt32(fs->bs.FATxx.FAT32.BS_FATSz32);
	}

	// check whether count of root dir entries is ok for given FAT type
	if (((fs->FATType == FATTYPE_FAT16) || (fs->FATType == FATTYPE_FAT12)) && (SwapInt16(fs->bs.BS_RootEntCnt) == 0)) {
		myerror("Count of root directory entries must not be zero for FAT1x!");
		fs_close(fs->fd);
		close(fs->rfd);
		return -1;	
	} else 	if ((fs->FATType == FATTYPE_FAT32) && (SwapInt16(fs->bs.BS_RootEntCnt) != 0)) {
		myerror("Count of root directory entries must be zero for FAT32 (%u)!", SwapInt16(fs->bs.BS_RootEntCnt));
		fs_close(fs->fd);
		close(fs->rfd);
		return -1;	
	}

	fs->clusters=getCountOfClusters(&(fs->bs));
	if (fs->clusters == -1) {
		myerror("Failed to get count of clusters!");
		fs_close(fs->fd);
		close(fs->rfd);
		return -1;
	}

	if (fs->clusters > 268435445) {
		myerror("Count of clusters should be less than 268435446, but is %d!", fs->clusters);
		fs_close(fs->fd);
		close(fs->rfd);
		return -1;
	}

	fs->sectorSize=SwapInt16(fs->bs.BS_BytesPerSec);

	fs->clusterSize=fs->bs.BS_SecPerClus * SwapInt16(fs->bs.BS_BytesPerSec);

	fs->FSSize = (u_int64_t) fs->clusters * fs->clusterSize;

	fs->maxDirEntriesPerCluster = fs->clusterSize / DIR_ENTRY_SIZE;

	fs->maxClusterChainLength = (u_int32_t) MAX_FILE_LEN / fs->clusterSize;

	u_int32_t rootDirSectors;

	rootDirSectors = ((SwapInt16(fs->bs.BS_RootEntCnt) * DIR_ENTRY_SIZE) +
			  (SwapInt16(fs->bs.BS_BytesPerSec) - 1)) / SwapInt16(fs->bs.BS_BytesPerSec);
	fs->firstDataSector = (SwapInt16(fs->bs.BS_RsvdSecCnt) +
			      (fs->bs.BS_NumFATs * fs->FATSize) + rootDirSectors);

	// convert utf 16 le to local charset
        fs->cd = iconv_open("//TRANSLIT", "UTF-16LE");
        if (fs->cd == (iconv_t)-1) {
                myerror("iconv_open failed!");
		return -1;
        }

	return 0;
}

int32_t syncFileSystem(struct sFileSystem *fs) {
/*
	sync file system
*/
	if (fflush(fs->fd) != 0) {
		myerror("Could not flush stream!");
		return -1;
	}
	if (fsync(fileno(fs->fd)) != 0) {
		myerror("Could not sync file descriptor!");
		return -1;
	}

	return 0;
}

int32_t closeFileSystem(struct sFileSystem *fs) {
/*
	closes file system
*/
	assert(fs != NULL);

	fs_close(fs->fd);
	if ((fs->mode == FS_MODE_RW_EXCL) || (fs->mode == FS_MODE_RO_EXCL))
		close(fs->rfd);
	iconv_close(fs->cd);

	return 0;
}

