# Compiler
CC = gcc

# Source files and directories
SOURCES = src/p_sheetmidi.c src/chord_data.c src/token_handler.c
CFLAGS = -I src -I src/include

# Detect OS and set appropriate extension and flags
UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
    EXTENSION = pd_darwin
    LDFLAGS = -bundle -undefined dynamic_lookup
    ARCHS = -arch x86_64 -arch arm64
else ifeq ($(UNAME), Linux)
    EXTENSION = pd_linux
    LDFLAGS = -shared -Wl,--export-dynamic
    ARCHS =
else
    EXTENSION = dll
    LDFLAGS = -shared
    ARCHS =
endif

# Target binary
TARGET = lib/p_sheetmidi.$(EXTENSION)

# Phony targets
.PHONY: all clean

# Default target
all: $(TARGET)

# Linking
$(TARGET): $(SOURCES)
	@mkdir -p lib
	$(CC) $(CFLAGS) $(LDFLAGS) $(ARCHS) -o $@ $^

# Cleaning
clean:
	rm -f $(TARGET) 