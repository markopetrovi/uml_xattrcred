SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=build/%.o)
CFLAGS ?= -O3 -mtune=native -march=native -Wall
BIN := uml_xattrcred

all: $(BIN)

build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf build/
	rm -f uml_xattrcred
