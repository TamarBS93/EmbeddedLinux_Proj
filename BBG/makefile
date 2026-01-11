# Makefile for BBG Communication System
CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = bbg_comm
SRC = i2c_tcp_client.c
OBJ = $(SRC:.c=.o)

# Default rule
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to generate Doxygen documentation
doc:
	doxygen Doxyfile

# Clean build files
clean:
	rm -f $(OBJ) $(TARGET)
	rm bbg_system.log

.PHONY: all clean doc