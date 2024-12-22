CC = gcc209
CCFLAGS = -g -O0 -no-pie -D_GNU_SOURCE
LDFLAGS = -no-pie

STUDENT_ID := 20210889
SUBMIT_DIR := .submit
SUBMIT_FILES := *.c *.h Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

TARGET = ish

all: $(TARGET)

# The main executable depends on these object files.
$(TARGET): ish.c dynarray.c token.c util.c lexsyn.c dynarray.h token.h util.h lexsyn.h
	$(CC) $(CCFLAGS) ish.c dynarray.c token.c util.c lexsyn.c -o $(TARGET)

# Submission archive
submit:
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	cd $(SUBMIT_DIR) && tar -czf ../$(SUBMIT) *
	rm -rf $(SUBMIT_DIR)

# Build clean
clean:
	rm -rf $(TARGET)

.PHONY: all clean submit

