#include <stdint.h>
#include <stddef.h>

typedef uint8_t   U8;
typedef int32_t   B32;
typedef int32_t   I32;
typedef uint32_t  U32;
typedef uint64_t  U64;
typedef float     F32;
typedef double    F64;
typedef uintptr_t UPtr;
typedef char      Byte;
typedef ptrdiff_t Size;
typedef size_t    USize;

#define Size_MASK ((unsigned)-1)
#define Size_MAX  ((Size)(Size_MASK >> 1))

#define SIZEOF(x)    (Size)sizeof(x)
#define ALIGNOF(x)   (Size)_Alignof(x)
#define COUNTOF(a)   (SIZEOF(a) / SIZEOF(a[0]))
#define LENGTHOF(s)  (COUNTOF(s) - 1)

#define MAKE(a, t, n, f) (t *)arena_alloc(a, SIZEOF(t), ALIGNOF(t), n, f)
#define MAKEONE(a, t, f) (t *)arena_alloc(a, SIZEOF(t), ALIGNOF(t), 1, f)

#if __GNUC__
    #define TRAP __builtin_unreachable()
    #define NORETURN __attribute__((noreturn))
#else
    #define TRAP *(volatile int *)0 = 0
    #define NORETURN
#endif

#define ASSERT(c) while (!(c)) TRAP

#define Str8(s) (Str8){(U8 *)s, LENGTHOF(s)}
typedef struct {
    U8   *s;
    Size  len;
} Str8;

static Str8 str8_make_from_span(U8* u, Size len) {
    Str8 s = { u, len };
    return s;
}

typedef struct {
    Byte *beg;
    Byte *end;
} Arena;

enum {
    ArenaFlags_NOZERO   = 1 << 0,
    ArenaFlags_SOFTFAIL = 1 << 1,
} ArenaFlags;

enum {
    MemSizes_B  = 1 << 0,
    MemSizes_KiB = 1 << 10,
    MemSizes_MiB = 1 << 20,
    MemSizes_GiB = 1 << 30,
} MemSizes;

//////////////////////////////////
// Platform API

void *memset(void*, int, size_t);

// Write buffer to stdout (1) or stderr (2). The platform must detect
// write errors and arrange for an eventual non-zero exit status.
// Platform must do total(not-partial) write. Return zero value means success
static B32 os_file_write(I32 fd, Str8);

// Immediately exit the program with a non-zero status.
NORETURN static void os_fail(void);

///////////////////////////////////
// Application

NORETURN static void oom(void);

static Byte *arena_alloc(Arena *a, Size size, Size align, Size count, I32 flags)
{
    ASSERT(size > 0);
    ASSERT(count > 0);
    Size avail = a->end - a->beg;
    Size padding = -(UPtr)a->beg & (align - 1);
    if (count > (avail - padding)/size) {
        if (flags & ArenaFlags_SOFTFAIL) {
            return 0;
        }
        oom();
    }
    Size total = size * count;
    Byte *p = a->beg + padding;
    a->beg += padding + total;
    return flags&ArenaFlags_NOZERO ? p : memset(p, 0, total);
}

///////////////////////////////////
// Output Buffer
typedef struct {
    U8*   buf;
    Size  cap;
    Size  len;
    I32   fd;
    B32   error;
} Buf;

Buf make_fdbuf(Arena *a, I32 fd, Size len) {
    U8* data = MAKE(a, U8, len, 0);
    Buf b = { data, len, 0, fd, 0 };
    return b;
}

void buf_flush(Buf* b) {
    b->error |= b->fd < 0;
    if (!b->error && b->len) {
	b->error |= os_file_write(b->fd, str8_make_from_span(b->buf, b->len));
	b->len = 0;
    }
}

static void buf_append(Buf* b, U8* src, Size len) {
    Size left = len;
    while (!b->error && left > 0) {
	Size avail = b->cap - b->len;
	Size amount = avail < left ? avail : left;
	for (Size i = 0; i < amount; i++) {
	    b->buf[b->len+i] = src[i];
	}
	b->len += amount;
	left -= amount;
	if (amount < left) {
	    buf_flush(b);
	}
    }
}

static void buf_append_str8(Buf* b, Str8 s) {
    return buf_append(b, s.s, s.len);
}

static void buf_append_u8(Buf* b, U8 x) {
    buf_append(b, &x, 1);
}

static void buf_append_i32(Buf* b, I32 x) {
    U8 tmp[64];
    U8* end = tmp + COUNTOF(tmp);
    U8* beg = end;
    I32 neg_x = x > 0 ? -x : x;
    do {
	*--beg = (U8)('0' - (neg_x % 10));
    } while (neg_x /= 10);
    if (x < 0) {
	*--beg = '-';
    }
    buf_append(b, beg, end-beg);
}

///////////////////////////////////
// WebClient

NORETURN static void oom(void)
{
    os_file_write(2, Str8("webclient: out of memory\n"));
    os_fail();
}

typedef struct {
  Str8 host;
  Str8 port;
  Str8 filename;
} WebClientParams;

typedef struct {
  Arena arena;
  WebClientParams params;
} WebClientState;

typedef struct {
  I32 socket_fd;
} WebClientConnection;

static void appmain(WebClientState state)
{
  Arena *a = &state.arena;

  Buf stdout = make_fdbuf(a, 1, 4 * MemSizes_KiB);
  buf_append_str8(&stdout, Str8("Hello!\n"));
  buf_flush(&stdout);
  
  /* i32 socket_fd = open_client_socket(state.params.host, state.params.port); */
  /* ASSERT(socket_fd > 2); */

  /* exit(0); */
}
