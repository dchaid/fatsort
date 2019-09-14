CC=gcc
LD=gcc

ifdef DEBUG
CFLAGS += -g -DDEBUG=$(DEBUG) -fstack-protector-all
DEBUG_OBJ=mallocv.o
else
LDFLAGS += -s
endif

CFLAGS += -Wall -Wextra
override CFLAGS+= -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64

INSTALL_FLAGS=-m 0755 -p -D

# Detect Mac OS X ($OSTYPE = darwin9.0 for Mac OS X 10.5 and darwin10.0 for Mac OS X 10.6)
UNAME=$(shell uname -s)
ifeq ($(UNAME),Darwin)
NEED_ICONV_LIB = 1
# OS X's install does not support the '-D' flag.
INSTALL_FLAGS=-m 0755 -p
endif

ifeq ($(OSTYPE),FreeBSD)
override CFLAGS += -I/usr/local/include
LDFLAGS += -L/usr/local/lib
NEED_ICONV_LIB = 1
endif

ifdef NEED_ICONV_LIB
LDFLAGS += -liconv
endif

# Mac OS X does not have a "/usr/local/sbin"
ifeq ($(UNAME),Darwin)
SBINDIR=/usr/local/bin
else
SBINDIR=/usr/local/sbin
endif

OBJ=fatsort.o FAT_fs.o fileio.o endianness.o signal.o entrylist.o errors.o options.o clusterchain.o sort.o misc.o natstrcmp.o stringlist.o regexlist.o

all: fatsort

fatsort: $(OBJ) $(DEBUG_OBJ) Makefile
	${LD} ${LDFLAGS} $(OBJ) $(DEBUG_OBJ) -o $@

fatsort.o: fatsort.c endianness.h signal.h FAT_fs.h platform.h options.h \
 stringlist.h errors.h sort.h clusterchain.h misc.h mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

FAT_fs.o: FAT_fs.c FAT_fs.h platform.h errors.h endianness.h fileio.h \
 mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

fileio.o: fileio.c fileio.h platform.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

endianness.o: endianness.c endianness.h mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

signal.o: signal.c signal.h mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

entrylist.o: entrylist.c entrylist.h FAT_fs.h platform.h options.h \
 stringlist.h errors.h natstrcmp.h mallocv.h endianness.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

errors.o: errors.c errors.h mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

options.o: options.c options.h platform.h FAT_fs.h stringlist.h regexlist.h errors.h \
 mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

clusterchain.o: clusterchain.c clusterchain.h platform.h errors.h \
 mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

sort.o: sort.c sort.h FAT_fs.h platform.h clusterchain.h entrylist.h \
 errors.h options.h stringlist.h regexlist.h endianness.h signal.h misc.h fileio.h \
 mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

misc.o: misc.c misc.h options.h platform.h FAT_fs.h stringlist.h \
 mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

natstrcmp.o: natstrcmp.c natstrcmp.h mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

stringlist.o: stringlist.c stringlist.h platform.h FAT_fs.h errors.h \
 mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

regexlist.o: regexlist.c regexlist.h platform.h FAT_fs.h errors.h \
 mallocv.h Makefile
	$(CC) ${CFLAGS} -c $< -o $@

mallocv.o: mallocv.c mallocv.h errors.h
	$(CC) ${CFLAGS} -c $< -o $@

install:
	install $(INSTALL_FLAGS) fatsort $(DESTDIR)$(SBINDIR)/fatsort
	
clean:
	rm -f *.o fatsort

.PHONY: all clean


