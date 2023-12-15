CC = gcc
CFLAGS = -std=c99 -g3 -Wall -Wextra -Wconversion -Wdouble-promotion -Wno-comment \
       -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion -fsanitize=undefined -fsanitize=thread

all: webclient

webclient: generic_webclient_main.c webclient.c
	$(CC) $(CFLAGS) generic_webclient_main.c -o webclient

tags:
	ctags -R -e .

clean:
	-rm -f webclient
	-rm -f tags TAGS
