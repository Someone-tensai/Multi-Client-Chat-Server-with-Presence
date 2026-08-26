CC      = gcc
CFLAGS  = -Wall -Wextra -I include
LDFLAGS = -lpthread -lsqlite3 -lssl -lcrypto

SERVER_SRCS = src/server.c src/client_handler.c src/registry.c src/protocol.c src/display.c src/threadpool.c src/db.c src/config.c src/log.c src/session.c src/receipt.c src/presence.c src/block.c src/permission.c src/invite.c src/redis.c src/pg.c src/metrics.c
CLIENT_SRCS = replies/client.c src/protocol.c src/display.c

SERVER_BIN  = server
CLIENT_BIN  = client

.PHONY: all clean

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lssl -lcrypto

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
