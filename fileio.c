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
	This file contains file io functions for UNIX/Linux
*/

#include "fileio.h"
#include <stdio.h>
#include <sys/types.h>

int fs_seek(FILE *stream, off_t offset, int whence) {
	return fseeko(stream, offset, whence);
}

off_t fs_read(void *ptr, u_int32_t size, u_int32_t n, FILE *stream) {
	return fread(ptr, size, n, stream);
}

off_t fs_write(const void *ptr, u_int32_t size, u_int32_t n, FILE *stream) {
	return fwrite(ptr, size, n, stream);
}

int fs_close(FILE* file) {
	return fclose(file);
}

