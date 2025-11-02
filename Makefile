# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -pthread -g -O0
LIBS = -lsqlite3 -lm

# Executable names
SERVER = parking_tcp_server
PRICEDB = pricing_db_handling

# Source files
SERVER_SRC = parking_tcp_server.c
PRICEDB_SRC = pricing_db_handling.c

all: $(SERVER) $(PRICEDB)

$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(PRICEDB): $(PRICEDB_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# --- Run both in parallel ---
run: all
	@echo "Starting pricing_db_handling and parking_tcp_server..."
	@./$(PRICEDB) & \
	sleep 1; \
	./$(SERVER)

# --- Clean build files ---
clean:
	rm -f $(SERVER) $(PRICEDB) parking.db


