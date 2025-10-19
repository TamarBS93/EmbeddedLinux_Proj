# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g
LIBS = -lsqlite3

# Source and output
SRC = parking_tcp_server.c
OUT = parking_tcp_server

# Default target
all: $(OUT)

# Build rule
$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LIBS)

# Clean rule
clean:
	rm -f $(OUT)
