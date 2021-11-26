CC=gcc
CFLAGS=-Wall -g -O2
TARGET=brute
OBJ=main.o
LIBS=-lcrypt -lpthread -lrt

all: $(TARGET)
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LIBS) $(OBJ) -o $@

main.o: main.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm -f $(OBJ) $(TARGET)
