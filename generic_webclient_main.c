// Generic C plaform layer for webclient
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

int main(int argc, char** argv) {
    WebClientState webclient = {0};
    webclient.arena = newarena_();

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

