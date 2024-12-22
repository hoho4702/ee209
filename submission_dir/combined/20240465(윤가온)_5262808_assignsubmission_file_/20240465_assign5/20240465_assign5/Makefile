STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES := *.c *.h Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

CC = gcc
CFLAGS = -Wall -g -D_GNU_SOURCE -std=c99
TARGET = ish
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, %.o, $(SRCS))

# Build the target
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

# Submit rule
submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	tar zcf ./$(SUBMIT) $(SUBMIT_DIR)
	rm -rf $(SUBMIT_DIR)

# Clean rule
clean:
	rm -rf $(TARGET) *.o

.PHONY: all clean submit
