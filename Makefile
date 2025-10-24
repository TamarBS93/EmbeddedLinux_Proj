# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -O0
LIBS = -lsqlite3 -lm

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
	rm -f $(OUT) *.db
