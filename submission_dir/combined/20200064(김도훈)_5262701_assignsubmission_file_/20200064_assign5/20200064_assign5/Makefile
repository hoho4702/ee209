STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES:= *.c *.h Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

TARGET = ish
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, %.o, $(SRCS))

# FIX THIS FILE
CC = gcc209
CFLAGS = -D_BSD_SOURCE -D_DEFAULT_SOURCE  -D_GNU_SOURCE

all: $(TARGET)
$(TARGET): $(OBJS) 

dynarray.o:
	$(CC) $(CFLAGS) -c dynarray.c

lexsyn.o:
	$(CC) $(CFLAGS) -c lexsyn.c

token.o:
	$(CC) $(CFLAGS) -c token.c

util.o:
	$(CC) $(CFLAGS) -c util.c

ish.o:
	$(CC) $(CFLAGS) -c ish.c

ish: ish.o dynarray.o lexsyn.o token.o util.o
	$(CC) ish.o dynarray.o lexsyn.o token.o util.o -o ish

submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	cd $(SUBMIT_DIR)/.. && tar zcf $(SUBMIT) $(SUBMIT_DIR)
	rm -rf $(SUBMIT_DIR)

clean:
	rm -rf $(TARGET) *.o

.PHONY: all clean submit
