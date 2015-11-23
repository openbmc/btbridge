CFLAGS	= $(shell pkg-config --cflags libsystemd) -Wall -O2
LDLIBS	= $(shell pkg-config --libs libsystemd)

EXE = btbridged

all: $(EXE)

clean:
	rm -rf *.o $(EXE)
