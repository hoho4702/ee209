STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES:= *.c *.h Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

TARGET = ish
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, %.o, $(SRCS))

CFLAGS = -g -Wall -Werror -D_DEFAULT_SOURCE -D_GNU_SOURCE -std=c99

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	cp dynarray.c dynarray.h $(SUBMIT_DIR)
	tar zcf ./$(SUBMIT) $(SUBMIT_DIR)
	rm -rf $(SUBMIT_DIR)

clean:
	rm -rf $(TARGET) *.o

.PHONY: all clean submit
