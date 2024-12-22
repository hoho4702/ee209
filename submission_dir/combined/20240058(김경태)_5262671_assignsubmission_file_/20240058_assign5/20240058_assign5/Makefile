CC = gcc
CFLAGS = -Wall -std=c99 -g -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_GNU_SOURCE
LDFLAGS =

SRCS = ish.c lexsyn.c util.c token.c dynarray.c
OBJS = $(SRCS:.c=.o)
EXEC = ish

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(LDFLAGS) -o $(EXEC) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJS) $(EXEC)

run: $(EXEC)
	./$(EXEC)

debug: CFLAGS += -g
debug: clean $(EXEC)

.PHONY: all clean run debug
