CC=gcc
CFLAGS=-Wall -g -std=c99
LIBS=-lcrypt -lpthread
DEPS=

OBJ=main.o common.o iterative.o recursive.o generator.o multithreaded.o singlethreaded.o queue.o sync-server.o
TARGET=brute

ifeq ($(shell uname), Darwin)
override CFLAGS+=-I./crypt-macos
override LIBS+=-L./crypt-macos
override DEPS+=crypt-macos/libcrypt.a
endif

all: $(TARGET) encr
$(TARGET): $(OBJ) $(DEPS)
	$(CC) $(CFLAGS) $(OBJ) $(LIBS) -o $@

encr: $(DEPS)
	$(CC) $(CFLAGS) encr.c $(LIBS) -o $@

clean:
	rm -f $(OBJ) $(TARGET) encr

ifeq ($(shell uname), Darwin)
crypt-macos/libcrypt.a:
	$(MAKE) -C crypt-macos
endif
