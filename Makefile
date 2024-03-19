##
# Light
#
# @file
# @version 0.1

CC=gcc
LFLAGS=-lddcutil
CFLAGS=-O3 -Wall -Wextra -Wpedantic -Wno-unused $(LFLAGS)

LIST=List/List.o
CLIENT=light.o $(LIST)
DAEMON=lightd.o $(LIST)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

all: light lightd

light: $(CLIENT)
	$(CC) -o $@ $^ $(CFLAGS)

lightd: $(DAEMON)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(DAEMON) $(CLIENT) light lightd

# end
