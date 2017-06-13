PROGRAM		     = dsbmc-cli
PREFIX		    ?= /usr/local
BINDIR               = ${PREFIX}/bin
MANDIR		     = ${PREFIX}/man/man1
CFLAGS		    += -Wall -DPROGRAM=\"${PROGRAM}\"
LIB		    += libdsbmc/libdsbmc.c
BSD_INSTALL_DATA    ?= install -m 0644
BSD_INSTALL_PROGRAM ?= install -s -m 555

all: ${PROGRAM}

${PROGRAM}: ${PROGRAM}.c
	${CC} -o ${PROGRAM} ${CFLAGS} ${CPPFLAGS} ${PROGRAM}.c ${LIB}

${PROGRAM}.1.gz: ${PROGRAM}.1
	gzip -k ${PROGRAM}.1

install: ${PROGRAM} ${PROGRAM}.1.gz
	${BSD_INSTALL_PROGRAM} ${PROGRAM} ${DESTDIR}${BINDIR}
	${BSD_INSTALL_DATA} ${PROGRAM}.1.gz ${DESTDIR}${MANDIR}
clean:
	-rm -f ${PROGRAM}
	-rm -f ${PROGRAM}.1.gz

