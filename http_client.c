#include <stdint.h>
#include <stddef.h>

typedef uint8_t   u8;
typedef int32_t   b32;
typedef int32_t   i32;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef float     f32;
typedef double    f64;
typedef uintptr_t uptr;
typedef ptrdiff_t isize;
typedef size_t    usize;

enum {
    mem_size_B   = 1,
    mem_size_KiB = 1 << 10,
    mem_size_MiB = 1 << 20,
    mem_size_GiB = 1 << 30,
} mem_sizes;

#if __GNUC__
    #define TRAP __builtin_unreachable()
    #define NORETURN __attribute__((noreturn))
#else
    #define TRAP *(volatile int *)0 = 0
    #define NORETURN
#endif

#define ASSERT(c) while (!(c)) TRAP


#define DEFAULTALIGNMENT (16)
#define SIZEOF(x)   (isize)sizeof(x)
#define COUNTOF(a)  (SIZEOF(a) / SIZEOF(a[0]))
#define CSTRLEN(s)  (COUNTOF(s) - 1)

#define str8(s) (str8){(u8 *)s, CSTRLEN(s)}
typedef struct {
    u8    *s;
    isize  len;
} str8;

static str8 str8_from_ptr(u8 *p, isize len) {
    str8 s = { p, len };
    return s;
}

typedef struct {
    char    *beg;
    char    *end;
} arena;

enum {
    arena_flag_NOZERO   = 1,
    arena_flag_SOFTFAIL = 1 << 1,
} arena_flags;

/* **** Platform API **** */
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);

static b32 os_file_write(i32, str8);

NORETURN static void os_fail(void);

static i32 tcp_open_client_socket(str8 address, str8 port);

typedef struct {
    i32 error;
    isize count;
} tcp_write_result;

static tcp_write_result tcp_write(i32, str8);

typedef struct {
    i32 error;
    isize count;
} tcp_read_result;

static tcp_read_result tcp_read(i32, u8 *, isize);

/* ********************** */


/* **** Application **** */

typedef struct {
    void *(*calloc)(isize, isize, void *ctx);
    void *(*malloc)(isize, void *ctx);
    void *(*realloc)(void *, isize, isize, void *ctx);
    void  (*free)(void *, isize, void *ctx);
    void *ctx;
} allocator;

NORETURN static void oom(void);
NORETURN static void oom(void) {
    os_file_write(2, str8("application: out of memory\n"));
    os_fail();
}

static void *arena_calloc(isize count, isize size, void *ctx) {
    ASSERT(size > 0);
    ASSERT(count > 0);
    arena *a = ctx;
    isize available = a->end - a->beg;
    isize alignment = -size & (DEFAULTALIGNMENT-1);
    if (count > (available-alignment)/size) {
	// TODO learn and implement longjmp
	// https://nullprogram.com/blog/2023/02/12/
	// https://nullprogram.com/blog/2023/12/17/
	oom();
    }
    isize total = (size * count) + alignment;
    a->end -= total;
    return memset(a->end, 0, total);
}

static void *arena_malloc(isize size, void *ctx) {
    return arena_calloc(1, size, ctx);
}

static void arena_free(void *ptr, isize size, void *ctx) {
    arena *a = ctx;
    if (ptr == a->end) {
	isize alignment = -size & (DEFAULTALIGNMENT-1);
	a->end += size + alignment;
    }
}

static void *arena_realloc(void *ptr, isize old, isize new, void *ctx) {
    // TODO Exercise for the reader: The last-allocated object can be resized in
    // place, instead using memmove. If this is frequently expected,
    // allocate from the front, adjust arena_free as needed, and
    // extend the allocation in place as discussed a previous
    // addendum, without any copying.
    // https://nullprogram.com/blog/2023/10/05/#addendum-extend-the-last-allocation
    ASSERT(new > old);
    arena *a = ctx;
    if (ptr == a->end) {
	void *r = arena_malloc(new - old, ctx);
	return memmove(r, ptr, old);
    }
    void *r = arena_malloc(new, ctx);
    return memcpy(r, ptr, old);
}

static allocator make_arena_allocator(arena *a) {
    allocator allocator = {0};
    allocator.calloc = arena_calloc;
    allocator.malloc = arena_malloc;
    allocator.free = arena_free;
    allocator.realloc = arena_realloc;
    allocator.ctx = a;
    return allocator;
}

/* ********************* */


// #define new(a, t, n) (t *)(a->сalloc(n, SIZEOF(t), a->ctx)

typedef struct {
    str8 host;
    str8 port;
    str8 filename;
} http_client_params;

typedef struct {
    arena arena;
    http_client_params params;
} http_client;

static void appmain(http_client client) {
    arena *a = &client.arena;
    allocator al = make_arena_allocator(a);

    isize slen = 16;
//    u8 *s = new(&al, u8, 16);
    u8 *s = (u8*)al.calloc(slen, SIZEOF(u8), al.ctx);
    str8 string = str8_from_ptr(s, slen);

    char *cstr = "Hello!\n";
    memcpy(string.s, cstr, CSTRLEN(cstr));

    os_file_write(1, string);
}
