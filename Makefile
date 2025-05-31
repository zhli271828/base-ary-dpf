TARGET = ./bin/test 
CC = gcc 
CFLAGS += -std=c99 -O3 -I./include -I/usr/include/openssl/
LDFLAGS = -march=native -lcrypto -lssl -lm -maes -ffast-math

SOURCES = $(wildcard ./src/*.c)
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p ./bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean