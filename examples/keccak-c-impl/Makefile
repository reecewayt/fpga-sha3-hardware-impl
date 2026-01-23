### Makefile for Keccak C implementation tests
### use make && make run to build and run the tests

CC = gcc
CFLAGS = -Wall -O2 -std=c99 #-DVERBOSE 	# Uncomment for verbose output
LDFLAGS = 


TARGET = keccak_test.out

SRCS = Keccak-readable-and-compact.c Keccak-compat-test.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
