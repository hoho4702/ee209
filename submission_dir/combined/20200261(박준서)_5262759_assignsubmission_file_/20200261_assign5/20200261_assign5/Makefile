CC = gcc
CFLAGS = -g -Wall -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_GNU_SOURCE
LDFLAGS =

SRCS = ish.c dynarray.c lexsyn.c token.c util.c
OBJS = $(SRCS:.c=.o)
TARGET = ish

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES := *.c *.h Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	tar zcf ./$(SUBMIT) $(SUBMIT_DIR)
	rm -rf $(SUBMIT_DIR)

clean:
	rm -f $(TARGET) $(OBJS)
	rm -f $(SUBMIT)

.PHONY: all clean submit