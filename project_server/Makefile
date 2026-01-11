CC = gcc
# -IInc tells gcc to look in the Inc folder for header files
CFLAGS = -Wall -Wextra -pthread -g -O0 -IInc
LIBS = -lsqlite3 -lm

TARGET = parking_system

# Directories
SRC_DIR = Src
INC_DIR = Inc
OBJ_DIR = Obj

# Find all .c files in Src and define their .o paths in Obj/
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

all: $(OBJ_DIR) $(TARGET)

# Link the objects into the final binary
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Compile .c to .o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Create the Obj directory if it doesn't exist
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

run: all
	@echo "Cleaning up and starting..."
	-@pkill -9 $(TARGET) 2>/dev/null || true
	@./$(TARGET)

clean:
	-@pkill -9 $(TARGET) 2>/dev/null || true
	rm -rf $(OBJ_DIR) $(TARGET) server_system.log