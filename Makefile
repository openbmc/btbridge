CPPFLAGS=$(shell pkg-config --cflags libsystemd)
LDFLAGS=$(shell pkg-config --libs libsystemd)

EXE = btbridged

all: $(EXE)

clean:
	rm -rf *.o $(EXE)
