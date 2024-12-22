STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES := *.c *.h Makefile README.md EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

TARGET = ish
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, %.o, $(SRCS))

CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_DEFAULT_SOURCE -D_GNU_SOURCE

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

submit: $(SUBMIT)

$(SUBMIT): $(SUBMIT_DIR)
	tar zcf $@ $(SUBMIT_DIR)
	rm -rf $(SUBMIT_DIR)

$(SUBMIT_DIR):
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)

clean:
	rm -f $(TARGET) $(OBJS) $(SUBMIT)
	rm -rf $(SUBMIT_DIR)

.PHONY: all clean submit
