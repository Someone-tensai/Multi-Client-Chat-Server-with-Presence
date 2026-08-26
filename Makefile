CC      = gcc
CFLAGS  = -Wall -Wextra -I include
LDFLAGS = -lpthread -lsqlite3 -lcrypto

SERVER_SRCS = src/server.c src/client_handler.c src/registry.c src/protocol.c src/display.c src/threadpool.c src/db.c
CLIENT_SRCS = replies/client.c src/protocol.c src/display.c

SERVER_BIN  = server
CLIENT_BIN  = client

.PHONY: all clean

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
