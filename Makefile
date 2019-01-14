
LDFLAGS=-lX11 -lobs
CFLAGS=-Wall -ggdb -O

all: x11-blocker-source.so x11-blocker-test

x11-blocker-source.o: x11-blocker.c
	$(CC) $(CFLAGS) $(LDFLAGS) -c -fPIC $< -o $@

x11-blocker-source.so: x11-blocker-source.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< -shared -o $@

x11-blocker-test: x11-blocker-source.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean: fake
	rm -f x11-blocker-test x11-blocker-source.so x11-blocker-source.o

.PHONY: fake
