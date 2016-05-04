CFLAGS	= $(shell pkg-config --cflags libsystemd) -Wall -O2 -g
LDLIBS	= $(shell pkg-config --libs libsystemd)

ifdef KERNEL_HEADERS
	CFLAGS += -I$(KERNEL_HEADERS)
endif

EXE = btbridged

all: $(EXE)

.PHONY += test
test: $(EXE) ipmi-bouncer bt-host

bt-host: bt-host.c
	gcc -shared -fPIC -ldl $(CFLAGS) $^ -o $@.so

clean:
	rm -rf *.o $(EXE)
	rm -rf bt-host.so ipmi-bouncer
