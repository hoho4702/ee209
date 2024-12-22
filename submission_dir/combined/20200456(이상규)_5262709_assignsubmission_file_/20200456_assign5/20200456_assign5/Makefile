STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES:= *.c *.h Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

OBJFILES = ish.o dynarray.o lexsyn.o token.o util.o
CC = gcc209
CFLAGS = -D_BSD_SOURCE
CFLAGS = -D_GNU_SOURCE

TARGET = ish
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, %.o, $(SRCS))

# FIX THIS FILE

all: $(TARGET)

$(TARGET): $(OBJS) 

ish: $(OBJFILES)
	$(CC) $(OBJFILES) -o ish

submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	tar zcf ./$(SUBMIT) $(SUBMIT_DIR)
	rm -rf $(SUBMIT_DIR)

clean:
	rm -rf $(TARGET) *.o

.PHONY: all clean submit
