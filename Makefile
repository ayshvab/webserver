CC = gcc
CFLAGS = -std=c99 -g3 -Wall -Wextra -Wconversion -Wdouble-promotion -Wno-comment \
       -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion -fsanitize=undefined -fsanitize=thread

all: http_client

http_client: generic_http_client_main.c http_client.c
	$(CC) $(CFLAGS) generic_http_client_main.c -o http_client

tags:
	ctags -R -e .

clean:
	-rm -f http_client
	-rm -f TAGS
