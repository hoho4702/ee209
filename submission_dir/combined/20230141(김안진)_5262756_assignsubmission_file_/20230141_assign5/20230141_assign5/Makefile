STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES := *.c *.h Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

# Target name
TARGET = ish

# Source files and object files
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, %.o, $(SRCS))

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Werror -std=c99 -D_GNU_SOURCE

LDFLAGS =

# Default rule: build the target
all: $(TARGET)

# Link the target
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Compile each source file into an object file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Submit rule: create a tarball for submission
submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	tar zcf ./$(SUBMIT) $(SUBMIT_DIR)
	rm -rf $(SUBMIT_DIR)

# Clean rule: remove generated files
clean:
	rm -rf $(TARGET) *.o

.PHONY: all clean submit