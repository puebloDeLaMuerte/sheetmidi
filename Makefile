CC = gcc
CFLAGS = -I src -I src/include
LDFLAGS = -bundle -undefined dynamic_lookup
ARCHS = -arch x86_64 -arch arm64

SOURCES = src/p_sheetmidi.c src/chord_data.c src/token_handler.c
TARGET = lib/p_sheetmidi.pd_darwin

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	@mkdir -p lib
	$(CC) $(CFLAGS) $(LDFLAGS) $(ARCHS) -o $@ $^

clean:
	rm -f $(TARGET) 