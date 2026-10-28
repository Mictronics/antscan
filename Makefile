CC = gcc
# Single source of truth for the version is package.json
VERSION := $(shell sed -n 's/^ *"version": *"\([^"]*\)".*/\1/p' package.json)
CPPFLAGS += -D_GNU_SOURCE -DANTSCAN_VERSION=\"$(VERSION)\"

DIALECT = -std=c18
CFLAGS += $(DIALECT) -O0 -g -W -D_DEFAULT_SOURCE -Wall -fno-common -Wmissing-declarations
LIBS = -lm -lrsvisa -lwebsockets -luv -ljson-c -lpthread
LDFLAGS =

all: antscan

server/%.o: server/%.c $(wildcard server/*.h) package.json
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

antscan: server/antscan.o server/head.o server/receiver.o server/loop.o
	$(CC) -g -o server/$@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm -f server/*.o server/antscan
