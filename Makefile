all: btbridged

btbridged: btbridged.c
	$(CC) $(CFLAGS) `pkg-config --cflags --libs libsystemd` $^ -o $@
