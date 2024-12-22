CC = gcc209
CCFLAGS = -D_DEFAULT_SOURCE -D_GNU_SOURCE

STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := .submit
SUBMIT_FILES:= ish.c Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

%.o: %.c
	$(CC) $(CCFLAGS) -c $<

all: ish

submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	cd $(SUBMIT_DIR) && tar -czf ../$(SUBMIT) *
	rm -rf $(SUBMIT_DIR)

test: ish
	./tools/test.py

ish: ish.o dynarray.o lexsyn.o util.o token.o
	$(CC) $(CCFLAGS) -o ish ish.o dynarray.o lexsyn.o util.o token.o
ish.o: dynarray.h lexsyn.h util.h
dynarray.o: dynarray.h
lexsyn.o : dynarray.h lexsyn.h util.h token.h
util.o : dynarray.h
token.o: token.h

debug: CCFLAGS += -g 
debug: all

clean:
	rm -rf ish *.o

.PHONY: all submit test clean debug
