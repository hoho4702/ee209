STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES:= *.c *.h Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

TARGET = ish
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, %.o, $(SRCS))

# FIX THIS FILE
CC = gcc209
CFLAGS = -D_DEFAULT_SOURCE -D_GNU_SOURCE


all: $(TARGET)

$(TARGET): $(OBJS) 

submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	tar zcf ./$(SUBMIT) $(SUBMIT_DIR)
	rm -rf $(SUBMIT_DIR)

clean:
	rm -rf $(TARGET) *.o

# ish : dynarray.o lexsyn.o token.o util.o
# 	$(CC) $(CFLAGS) dynarray.o lexsyn.o token.o util.o -o ish

# util.o : dynarray.h util.h
# 	$(CC) $(CFLAGS) dynarray.h util.h


.PHONY: all clean submit
