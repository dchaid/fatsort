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
	This functions are used to convert endianness of integers.
*/

#ifndef __endianness_h__
#define __endianness_h__

/*
	supports different byte-orders
*/

#ifndef __BIG_ENDIAN__
#define SwapInt16(i) i
#define SwapInt32(i) i
#else

#include <sys/types.h>
#include "platform.h"

// swaps endianness of a 16 bit integer
u_int16_t SwapInt16(u_int16_t value);

// swaps endianness of a 32 bit integer
u_int32_t SwapInt32(u_int32_t value);

#endif

#endif //__ endianness_h__
