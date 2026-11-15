CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= /usr/local

xntsrv-bridge: xntsrv-bridge.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

static: LDFLAGS += -static
static: xntsrv-bridge

install: xntsrv-bridge
	install -D -m 0755 xntsrv-bridge $(DESTDIR)$(PREFIX)/bin/xntsrv-bridge

clean:
	rm -f xntsrv-bridge

.PHONY: static install clean
