CFLAGS	= $(shell pkg-config --cflags libsystemd) -Wall -O2
LDLIBS	= $(shell pkg-config --libs libsystemd)

ifdef KERNEL_HEADERS
	CFLAGS += -I$(KERNEL_HEADERS)
endif

EXE = btbridged

all: $(EXE)

clean:
	rm -rf *.o $(EXE)
