PROGRAM		     = dsbmc-cli
PREFIX		    ?= /usr/local
BINDIR               = ${PREFIX}/bin
MANDIR		     = ${PREFIX}/man/man1
CFLAGS		    += -Wall -DPROGRAM=\"${PROGRAM}\"
LIB		    += libdsbmc/libdsbmc.c -lpthread
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

readme: readme.mdoc
	mandoc -mdoc readme.mdoc | perl -e 'foreach (<STDIN>) { \
		$$_ =~ s/(.)\x08\1/$$1/g; $$_ =~ s/_\x08(.)/$$1/g; print $$_ \
	}' | sed '1,1d' > README

readmemd: readme.mdoc
	mandoc -mdoc -Tmarkdown readme.mdoc | sed '1,1d; $$,$$d' > README.md

clean:
	-rm -f ${PROGRAM}
	-rm -f ${PROGRAM}.1.gz

