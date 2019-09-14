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
	This file contains platform dependend macros.
*/

#ifndef __platform_h__
#define __platform_h__

#if defined(linux) || defined(__linux) || defined(__linux__)
#undef __LINUX__
#define __LINUX__	1
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
#undef __BSD__
#define __BSD__		1
#endif


#endif // __platform_h__
