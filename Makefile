PROGRAM		     = dsbmc-cli
PREFIX		    ?= /usr/local
BINDIR               = ${PREFIX}/bin
CFLAGS		    += -Wall -DPROGRAM=\"${PROGRAM}\"
LIB		    += libdsbmc/libdsbmc.c
BSD_INSTALL_PROGRAM ?= install -s -m 555

all: ${PROGRAM}

${PROGRAM}: ${PROGRAM}.c
	${CC} -o ${PROGRAM} ${CFLAGS} ${CPPFLAGS} ${PROGRAM}.c ${LIB}

install: ${PROGRAM}
	${BSD_INSTALL_PROGRAM} ${PROGRAM} ${DESTDIR}${BINDIR}

clean:
	-rm -f ${PROGRAM}

