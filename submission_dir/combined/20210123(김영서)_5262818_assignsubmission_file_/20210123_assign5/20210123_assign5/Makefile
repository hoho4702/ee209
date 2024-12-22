CC ?= gcc209
CFLAGS += -g -std=c99
STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES:= dynarray.c dynarray.h ish.c lexsyn.c lexsyn.h Makefile readme token.c token.h util.c util.h EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

TARGET = ish

all: $(TARGET)

dynarray.o: dynarray.h dynarray.c
	$(CC) $(CFLAGS) -c -o dynarray.o dynarray.c -D_GNU_SOURCE -D_DEFAULT_SOURCE

ish.o: dynarray.h lexsyn.h token.h util.h dynarray.c lexsyn.c token.c util.c ish.c
	$(CC) $(CFLAGS) -c -o ish.o ish.c -D_GNU_SOURCE -D_DEFAULT_SOURCE

ish: ish.o lexsyn.o token.o util.o dynarray.o
	$(CC) $(CFLAGS) -o $@ $^ -D_GNU_SOURCE -D_DEFAULT_SOURCE

lexsyn.o: token.h util.h token.c util.c lexsyn.c
	$(CC) $(CFLAGS) -c -o lexsyn.o lexsyn.c -D_GNU_SOURCE -D_DEFAULT_SOURCE

token.o: token.h token.c
	$(CC) $(CFLAGS) -c -o token.o token.c -D_GNU_SOURCE -D_DEFAULT_SOURCE

util.o: dynarray.h util.h dynarray.c util.c
	$(CC) $(CFLAGS) -c -o util.o util.c -D_GNU_SOURCE -D_DEFAULT_SOURCE 

submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	tar zcf ./$(SUBMIT) $(SUBMIT_DIR)
	rm -rf $(SUBMIT_DIR)

clean:
	rm -rf $(TARGET) *.o

.PHONY: all clean submit
