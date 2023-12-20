#define _POSIX_C_SOURCE 200809L

#include "http_client.c"

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // memset, memcpy, memmove

static str8 fromcstr_(char* z) {
    str8 s = {(u8*)z, 0};
    if (s.s) {
	for (; s.s[s.len]; s.len++) {}
    }
    return s;
}

static arena newarena_(void) {
    isize cap = (isize)256 * (isize)mem_size_MiB;
    #ifdef DEBUG
    cap = (isize)1 * (isize)mem_size_MiB;
    #endif

    arena arena = {0};
    arena.beg = (char*)malloc(cap);
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
    http_client client = { 0 };
    client.arena = newarena_();

    struct optparse_long longopts[] = {
	{ "port", 'p', OPTPARSE_REQUIRED },
	{ "host", 'a', OPTPARSE_REQUIRED },
	{ "help", 'h', OPTPARSE_NONE },
	{ 0 },
    };
    int option;
    struct optparse options;
    (void)argc;
    optparse_init(&options, argv);
    while ((option = optparse_long(&options, longopts, NULL)) != -1) {
	switch (option) {
	case 'a':
	    client.params.host = fromcstr_(options.optarg);
	    break;
	case 'p':
	    client.params.port = fromcstr_(options.optarg);
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
	client.params.filename = fromcstr_(filename);
	break;
    }

    appmain(client);
    return ferror(stdout);
}

///////////////////////////////////////////// 

static void os_fail(void) {
    exit(1);
}

static i32 os_file_write(int fd, str8 s) {
    ASSERT(fd==1 || fd==2);
    FILE *f = fd==1 ? stdout : stderr;
    fwrite(s.s, s.len, 1, f);
    fflush(f);
    return ferror(f);
}

#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

static tcp_write_result tcp_write(i32 sockfd, str8 s) {
    tcp_write_result result = {0};
    u8 *beg = s.s;
    u8 *end = s.s + s.len;
    isize retval = 0;
    for (; beg < end; beg += retval) {
	retval = send(sockfd, beg, end-beg, 0);
	if (retval == -1) {
	    result.error = errno;
	    perror("tcp_write error");
	    break;
	}
	if (retval > 0) {
	    result.count += retval;
	}
    }
    return result;
}

