CC = gcc

TARGET = mkogg
SRC = src/mkogg.c

CFLAGS = -O2 -Wall -Wextra
LIBS = -lvorbis -lvorbisenc -logg -lm

PREFIX ?= /usr/local

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all install uninstall clean run
