CC=gcc
C_SOURCES=$(wildcard *.c)
OBJ=$(C_SOURCES:.c=.o)
CCFLAGS = -lgadu -lpthread -lstrophe -lconfig

all: build

build: $(OBJ)

clean:
	rm -rf *.o

%.o: %.c
	$(CC) $(CCFLAGS) $< -o $@

debug: build
	gdb -ex run ./ggtoxmpp.o

run: build
	./ggtoxmpp.o
