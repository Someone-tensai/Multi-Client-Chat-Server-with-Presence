CC = gcc
CFLAGS = -Wall -Wextra -g -pthread

SRC = src/server.c src/client_handler.c src/registry.c src/protocol.c src/display.c
OBJ = $(SRC:.c=.o)

all: server

server: $(OBJ)
	$(CC) $(CFLAGS) -o server $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f server *.o src/*.o