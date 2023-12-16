// Generic C plaform layer for webclient
#define _POSIX_C_SOURCE 200809L

#include "webclient.c"

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // memset

static Str8 fromcstr_(char* z) {
    Str8 s = {(U8 *)z, 0};
    if (s.s) {
	for (; s.s[s.len]; s.len++) {}
    }
    return s;
}

static Arena newarena_(void)
{
    Size cap = (Size)256 * (Size)MemSizes_MiB; 
    #ifdef DEBUG
    // Reduce for fuzzing and faster debugging
    cap = (Size)4 * (Size)MemSize_MiB;
    #endif

    Arena arena = {0};
    arena.beg = (Byte *)malloc(cap);
    arena.end = arena.beg ? arena.beg + cap : 0;
    return arena;
}

#define OPTPARSE_IMPLEMENTATION
#define OPTPARSE_API static
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include "third_party/optparse.h"
#pragma GCC diagnostic pop

int main(int argc, char** argv) {
    WebClient webclient = { 0 };
    webclient.arena = newarena_();

    struct optparse_long longopts[] = {
	{ "port", 'p', OPTPARSE_REQUIRED },
	{ "host", 'i', OPTPARSE_REQUIRED },
	{ "help", 'h', OPTPARSE_NONE },
	{ 0 },
    };
    int option;
    struct optparse options;
    (void)argc;
    optparse_init(&options, argv);
    while ((option = optparse_long(&options, longopts, NULL)) != -1) {
	switch (option) {
	case 'i':
	    webclient.params.host = fromcstr_(options.optarg);
	    break;
	case 'p':
	    webclient.params.port = fromcstr_(options.optarg);
	    break;
	case 'h':
	    printf("Usage: %s --host <host> --port <port> <filename>\n", argv[0]);
	    return ferror(stdout);
	case '?':
	    fprintf(stderr, "%s: %s\n", argv[0], options.errmsg);
	    exit(EXIT_FAILURE);
	}
    }

    char* filename;
    while ((filename = optparse_arg(&options))) {
	webclient.params.filename = Str8(filename);
	break;
    }

    appmain(webclient);
    return ferror(stdout);
}

static B32 os_file_write(int fd, Str8 s)
{
    ASSERT(fd==1 || fd==2);
    FILE* f = fd==1 ? stdout : stderr;
    fwrite(s.s, s.len, 1, f);
    fflush(f);
    return ferror(f);
}

/* static void os_tcp_write(int fd, Str8 s) */
/* { */
/*     // TODO */
/* } */

static void os_fail(void)
{
    exit(1);
}

///////////////////////////////////
// Network API

#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

I32 open_client_socket(Str8 hostname, Str8 port) {
    I32 client_fd;
    I32 rv;
    struct addrinfo hints;
    struct addrinfo* servinfo;
    struct addrinfo* cur_servinfo;

    memset(&hints, 0, SIZEOF(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // I "know" that this Str8.s point to null-terminated string
    // TODO: make proper cstr from Str8
    rv = getaddrinfo((char*)hostname.s, (char*)port.s, &hints, &servinfo);
    if (rv != 0) {
	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
	os_fail();
    }

    // loop through all the results and connect to the first we can
    for (cur_servinfo = servinfo; cur_servinfo != NULL; cur_servinfo = cur_servinfo->ai_next) {
	if ((client_fd = socket(cur_servinfo->ai_family,
		                cur_servinfo->ai_socktype,
		                cur_servinfo->ai_protocol)) == -1) {
	    perror("socket");
	    continue;
	}

	if (connect(client_fd, cur_servinfo->ai_addr, cur_servinfo->ai_addrlen) == -1) {
	    perror("connect");
	    close(client_fd);
	    continue;
	}

	break; // if we get here, we must have connected successfully
    }

    if (cur_servinfo == NULL) {
	fprintf(stderr, "failed to connect\n");
	os_fail();
    }

    freeaddrinfo(servinfo);

    return client_fd;
}
