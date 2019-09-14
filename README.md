# FATSort Utility

## Information

FATSort is a C utility that sorts FAT12, FAT16 and FAT32 partitions. It even can handle long file name entries. It was developed because I wanted to sort my MP3 files on my MP3 hardware player. Unfortunately, there was no utility out there so far, so I had to write it myself. FATSort reads the boot sector and sorts the directory structure recursively. FATSort is released under GPLv2.

## Supported platforms

• Linux
• BSD and other UNIX systems
• MacOS X

## Compilation

Just run the Makefile in the src directory with ```make```. The fatsort executable will be built.

## Installation

Just run ```make install``` to install FATSort.

So far, FATSort is included in Fedora, Debian, Ubuntu, and Gentoo. 

## Created from https://fatsort.sourceforge.io/
