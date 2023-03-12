CC := gcc
CFLAGS := -Wall

server: server.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clang-format clean
clang-format:
	clang-format -style=file -i *.c

clean:
	rm -f server
