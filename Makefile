
LDFLAGS=-lX11 -lobs
CFLAGS=-Wall

all: x11-blocker-source.so x11-blocker-test

x11-blocker-source.o: x11-blocker.c
	cc -c -fPIC $< -o $@

x11-blocker-source.so: x11-blocker-source.o
	cc $< -shared -o $@

x11-blocker-test: x11-blocker-source.o
	cc $(CFLAGS) $(LDFLAGS) $< -o $@

clean: fake
	rm -f x11-blocker-test x11-blocker-source.so x11-blocker-source.o

.PHONY: fake
