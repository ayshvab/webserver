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

// Network API
static I32 open_client_socket(Str8 hostname, Str8 port);

static I32 os_tcp_write(I32, Str8);

typedef struct {
    I32 error_code; // errno code
    B32 eof;
    B32 ok;
} OS_TCP_Read_Result;

static OS_TCP_Read_Result os_tcp_read(I32, U8*, Size);

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
enum buffer_type_t {
    BUFFER_TYPE_MEM = 0,
    BUFFER_TYPE_FILE,
    BUFFER_TYPE_TCP,
};

typedef struct {
    U8*   buf;
    Size  cap;
    Size  len;
    I32   fd;
    B32   error;
    enum buffer_type_t buf_type;
} Buf;

Buf make_fdbuf(Arena *a, I32 fd, Size len) {
    U8* data = MAKE(a, U8, len, 0);
    Buf b = { data, len, 0, fd, 0, BUFFER_TYPE_FILE };
    return b;
}

Buf buffer_tcp_make(Arena* a, I32 fd, Size len) {
    U8* data = MAKE(a, U8, len, 0);
    Buf b = { data, len, 0, fd, 0, BUFFER_TYPE_TCP };
    return b;
}

// TODO: os_*_write(I32, Str8) ----> os_*_write(I32, U8*, Size len)
void buf_flush(Buf* b) {
    b->error |= b->fd < 0;
    if (!b->error && b->len) {
	B32 result = -1;
	switch (b->buf_type) {
	default:
	    os_fail();
	case BUFFER_TYPE_FILE:
	    result = os_file_write(b->fd, str8_make_from_span(b->buf, b->len));
	    break;
	case BUFFER_TYPE_TCP:
	    result = os_tcp_write(b->fd, str8_make_from_span(b->buf, b->len));
	}
	b->error |= result;
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
} WebClient;

typedef struct {
    I32 socket_fd;
    Buf tcp_buffer_out;
} WebClientConnection;

static void webClient_print_params(WebClient* self, Buf* out) {

    buf_append_str8(out, Str8("======= WebClientParams =====\n"));
    buf_append_str8(out, Str8("host: "));
    buf_append_str8(out, self->params.host);
    buf_append_str8(out, Str8("\n"));

    buf_append_str8(out, Str8("port: "));
    buf_append_str8(out, self->params.port);
    buf_append_str8(out, Str8("\n"));

    buf_append_str8(out, Str8("filename: "));
    buf_append_str8(out, self->params.filename);
    buf_append_str8(out, Str8("\n"));
    buf_append_str8(out, Str8("=============================\n"));
}

static B32 http_send(WebClientConnection* self, Str8 host, Str8 filename) {
    Buf* tcp_out = &self->tcp_buffer_out;
    
    buf_append_str8(tcp_out, Str8("GET "));
    buf_append_str8(tcp_out, filename);
    buf_append_str8(tcp_out, Str8(" HTTP/1.1\r\n"));
		    
    buf_append_str8(tcp_out, Str8("Host: "));
    buf_append_str8(tcp_out, host);
    buf_append_str8(tcp_out, Str8("\r\n"));
    
    buf_append_str8(tcp_out, Str8("Connection: close\r\n"));
    buf_append_str8(tcp_out, Str8("\r\n"));
    
    buf_flush(tcp_out);
    return tcp_out->error;
}

static B32 http_receive(WebClientConnection* self, Buf* std_out) {
    Size received_bytes = 0;
    U8 value;
    OS_TCP_Read_Result result = {0};
    do {
	result = os_tcp_read(self->socket_fd, &value, SIZEOF(value));
	if (result.ok) {
	    received_bytes += SIZEOF(value);
	    buf_append_u8(std_out, value);
	} else if (result.eof) {
	    buf_flush(std_out);
	} else if (result.error_code) {
	    buf_flush(std_out);
	    buf_append_str8(std_out, Str8("\nFailed to read from socket. Error code: "));
	    buf_append_i32(std_out, result.error_code);
	    buf_append_str8(std_out, Str8("\n"));
	    buf_flush(std_out);
	    break;
	}
    } while(result.ok);

    return result.ok;
}

static void appmain(WebClient client)
{
  Arena *a = &client.arena;

  Buf stdout = make_fdbuf(a, 1, 4 * MemSizes_KiB);
  webClient_print_params(&client, &stdout);
  buf_flush(&stdout);

  WebClientConnection connection = {0};
  I32 socket_fd = open_client_socket(client.params.host, client.params.port);
  ASSERT(socket_fd > 2);
  connection.socket_fd = socket_fd;
  Buf tcp_out = buffer_tcp_make(a, socket_fd, 4 * MemSizes_KiB);
  connection.tcp_buffer_out = tcp_out;

  http_send(&connection, client.params.host, client.params.filename);

  http_receive(&connection, &stdout);
}
