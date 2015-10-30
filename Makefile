CPPFLAGS=$(shell pkg-config --cflags libsystemd)
LDFLAGS=$(shell pkg-config --libs libsystemd)

all: btbridged
