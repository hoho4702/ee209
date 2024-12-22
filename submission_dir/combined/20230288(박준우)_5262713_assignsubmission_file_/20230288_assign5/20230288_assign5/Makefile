STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES:= *.c *.h Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

CC := gcc209 -g -D_DEFAULT_SOURCE -D_GNU_SOURCE 

TARGET = ish
OBJS = ish.o wrapper.o dynarray.o token.o util.o lexsyn.o job.o 

# FIX THIS FILE

all: $(TARGET)

$(TARGET): $(OBJS) 

submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	tar zcf ./$(SUBMIT) $(SUBMIT_DIR)

clean:
	rm -rf $(TARGET) *.o

.PHONY: all clean submit
