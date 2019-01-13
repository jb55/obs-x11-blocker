
LDFLAGS=-lX11

x11-blocker-test: x11-test.o
	cc $(LDFLAGS) $< -o $@
