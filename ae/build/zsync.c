#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <setjmp.h>
#include "aether_panic.h"
#include "aether_stringseq.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>      // _setmode, _fileno
#include <fcntl.h>   // _O_BINARY
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#include <unistd.h>
#include <sched.h>
#endif
#ifdef _WIN32
#  define aether_aligned_alloc(align, size) _aligned_malloc((size), (align))
#else
#  define aether_aligned_alloc(align, size) aligned_alloc((align), (size))
#endif
#ifndef likely
#  if defined(__GNUC__) || defined(__clang__)
#    define likely(x)   __builtin_expect(!!(x), 1)
#    define unlikely(x) __builtin_expect(!!(x), 0)
#  else
#    define likely(x)   (x)
#    define unlikely(x) (x)
#  endif
#endif
#ifndef AETHER_GCC_COMPAT
#  if (defined(__GNUC__) || defined(__clang__)) && !defined(__EMSCRIPTEN__)
#    define AETHER_GCC_COMPAT 1
#  else
#    define AETHER_GCC_COMPAT 0
#  endif
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#ifdef _WIN32
static inline int64_t _aether_clock_ns(void) {
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (int64_t)((double)now.QuadPart / freq.QuadPart * 1000000000.0);
}
#elif defined(__EMSCRIPTEN__)
static inline int64_t _aether_clock_ns(void) {
    return (int64_t)(emscripten_get_now() * 1000000.0);
}
#elif defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0)
static inline int64_t _aether_clock_ns(void) { return 0; }
#else
static inline int64_t _aether_clock_ns(void) {
    struct timespec _ts;
    clock_gettime(CLOCK_MONOTONIC, &_ts);
    return (int64_t)_ts.tv_sec * 1000000000LL + _ts.tv_nsec;
}
#endif
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
extern void* aether_caps_malloc(size_t bytes);
static inline const char* aether_uniform_heap_str(const char* s, int is_heap) {
    if (!s) return (const char*)0;
    if (is_heap) return s;
    /* AetherString-aware length probe — see is_aether_string in
     * std/string/aether_string.h. Byte-by-byte to stay ASan-clean
     * on short literal allocations (e.g. "x"). */
    const unsigned char* _p = (const unsigned char*)s;
    const char* _data = s;
    size_t _n;
    if (_p[0] == 0xDE && _p[1] == 0xC0 && _p[2] == 0x57 && _p[3] == 0xAE) {
        /* Struct layout: magic(u32), ref_count(i32), length(size_t),
         * capacity(size_t), data(char*). Read length and data via
         * a typed view — the struct's data pointer is what we copy. */
        struct _AeStrHdr { unsigned int magic; int ref_count; size_t length; size_t capacity; char* data; };
        const struct _AeStrHdr* _h = (const struct _AeStrHdr*)s;
        _n = _h->length;
        _data = _h->data ? _h->data : s;
    } else {
        _n = strlen(s);
    }
    char* _d = (char*)aether_caps_malloc(_n + 1);
    if (!_d) return (const char*)0;
    if (_n) memcpy(_d, _data, _n);
    _d[_n] = '\0';
    return (const char*)_d;
}
extern void string_release(const char*);
static inline void aether_heap_str_free(const char* s) {
    if (!s) return;
    const unsigned char* _hp = (const unsigned char*)s;
    if (_hp[0] == 0xDE && _hp[1] == 0xC0 && _hp[2] == 0x57 && _hp[3] == 0xAE) {
        string_release(s);
    } else {
        free((void*)s);
    }
}
#include <stdarg.h>
static void* _aether_interp(const char* fmt, ...) {
    va_list args, args2;
    va_start(args, fmt);
    va_copy(args2, args);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    char* str = (char*)malloc(len + 1);
    vsnprintf(str, len + 1, fmt, args2);
    va_end(args2);
    return (void*)str;
}
extern const char* aether_string_data(const void* s);
extern size_t aether_string_length(const void* s);
extern void* string_new_with_length(const char* data, int length);
extern int list_add_string_owned(void* list, void* item);
extern int map_put_string_owned(void* map, const char* key, void* value);
static inline const char* _aether_safe_str(const void* s) {
    if (!s) return "(null)";
    return aether_string_data(s);
}
static inline const char* _aether_duration_repr(int64_t ns) {
    static char _buf[64];
    int64_t abs_ns = ns < 0 ? -ns : ns;
    struct _du { const char* suffix; int64_t scale; } units[] = {
        {"d", 86400000000000LL}, {"h", 3600000000000LL},
        {"m", 60000000000LL}, {"s", 1000000000LL},
        {"ms", 1000000LL}, {"us", 1000LL}, {"ns", 1LL}
    };
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        if (abs_ns >= units[i].scale || units[i].scale == 1) {
            if (ns % units[i].scale == 0) {
                snprintf(_buf, sizeof(_buf), "%lld%s", (long long)(ns / units[i].scale), units[i].suffix);
            } else {
                double v = (double)ns / (double)units[i].scale;
                snprintf(_buf, sizeof(_buf), "%.9g%s", v, units[i].suffix);
            }
            return _buf;
        }
    }
    return "0ns";
}
extern void aether_sleep_ms(int ms);
#if !AETHER_GCC_COMPAT
static void* _aether_ref_new(intptr_t val) { intptr_t* r = malloc(sizeof(intptr_t)); *r = val; return (void*)r; }
#endif
typedef struct { void (*fn)(void); void* env; } _AeClosure;
static inline void* _aether_box_closure(_AeClosure c) { _AeClosure* p = malloc(sizeof(_AeClosure)); *p = c; return (void*)p; }
static inline _AeClosure _aether_unbox_closure(void* p) { return *(_AeClosure*)p; }
typedef struct { _AeClosure compute; intptr_t value; int evaluated; } _AeThunk;
static inline void* _aether_thunk_new(_AeClosure c) { _AeThunk* t = malloc(sizeof(_AeThunk)); t->compute = c; t->value = 0; t->evaluated = 0; return (void*)t; }
static inline intptr_t _aether_thunk_force(void* p) { _AeThunk* t = (_AeThunk*)p; if (!t->evaluated) { t->value = (intptr_t)((intptr_t(*)(void*))t->compute.fn)(t->compute.env); t->evaluated = 1; } return t->value; }
static inline void _aether_thunk_free(void* p) { if (p) free(p); }
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1) && !defined(__arm__) && !defined(__thumb__)
#include <termios.h>
static struct termios _aether_orig_termios;
static void _aether_raw_mode(void) {
    tcgetattr(0, &_aether_orig_termios);
    struct termios raw = _aether_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &raw);
}
static void _aether_cooked_mode(void) {
    tcsetattr(0, TCSANOW, &_aether_orig_termios);
}
#else
static void _aether_raw_mode(void) {}
static void _aether_cooked_mode(void) {}
#endif
static void* _aether_ctx_stack[64];
static int _aether_ctx_depth = 0;
static inline void _aether_ctx_push(void* ctx) { if (_aether_ctx_depth < 64) _aether_ctx_stack[_aether_ctx_depth++] = ctx; }
static inline void _aether_ctx_pop(void) { if (_aether_ctx_depth > 0) _aether_ctx_depth--; }
static inline void* _aether_ctx_get(void) { return _aether_ctx_depth > 0 ? _aether_ctx_stack[_aether_ctx_depth-1] : (void*)0; }

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

void aether_args_init(int argc, char** argv);


typedef struct { const char* _0; int _1; const char* _2; } _tuple_string_int_string;
typedef struct { int _0; int _1; const char* _2; } _tuple_int_int_string;
typedef struct { int _0; const char* _1; } _tuple_int_string;
typedef struct { const char* _0; int _1; } _tuple_string_int;
typedef struct { void* _0; const char* _1; } _tuple_ptr_string;
typedef struct { int _0; int _1; int _2; int _3; } _tuple_int_int_int_int;
typedef struct { int _0; int _1; } _tuple_int_int;
typedef struct { const char* _0; int _1; int _2; const char* _3; } _tuple_string_int_int_string;
typedef struct { const char* _0; const char* _1; } _tuple_string_string;

typedef struct LocalTime LocalTime;
typedef struct Control Control;
typedef struct State State;
typedef struct ByteRange ByteRange;
typedef struct IntCell IntCell;
typedef struct HashEntry HashEntry;
typedef struct Stats Stats;
typedef struct RcksumState RcksumState;
typedef struct RSum RSum;
typedef struct BlockPair BlockPair;
typedef struct BlockRanges BlockRanges;
typedef struct LocalTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int nanos;
    int tz_offset_minutes;
} LocalTime;

typedef struct Control {
    void* rs;
    int filelen;
    int blocks;
    int blocksize;
    const char* checksum;
    const char* checksum_method;
    const char* filename;
    const char* mtime;
    void* urls;
    int ok;
    const char* err;
    int _heap_checksum;
    int _heap_checksum_method;
    int _heap_filename;
    int _heap_mtime;
    int _heap_err;
} Control;
static inline void Control_destroy(Control* s) {
    if (!s) return;
    if (s->_heap_checksum) { aether_heap_str_free(s->checksum); s->checksum = (const char*)0; s->_heap_checksum = 0; }
    if (s->_heap_checksum_method) { aether_heap_str_free(s->checksum_method); s->checksum_method = (const char*)0; s->_heap_checksum_method = 0; }
    if (s->_heap_filename) { aether_heap_str_free(s->filename); s->filename = (const char*)0; s->_heap_filename = 0; }
    if (s->_heap_mtime) { aether_heap_str_free(s->mtime); s->mtime = (const char*)0; s->_heap_mtime = 0; }
    if (s->_heap_err) { aether_heap_str_free(s->err); s->err = (const char*)0; s->_heap_err = 0; }
}

typedef struct State {
    void* ctl;
    void* rs;
    int filelen;
    int blocks;
    int blocksize;
    int fd;
} State;

typedef struct ByteRange {
    int start;
    int fin;
} ByteRange;

typedef struct IntCell {
    int v;
} IntCell;

typedef struct HashEntry {
    int next;
    int rsum_a;
    int rsum_b;
    const char* md4;
    int _heap_md4;
} HashEntry;
static inline void HashEntry_destroy(HashEntry* s) {
    if (!s) return;
    if (s->_heap_md4) { aether_heap_str_free(s->md4); s->md4 = (const char*)0; s->_heap_md4 = 0; }
}

typedef struct Stats {
    int hash_hit;
    int weak_hit;
    int strong_hit;
    int checksummed;
} Stats;

typedef struct RcksumState {
    int blocks;
    int block_size;
    int block_shift;
    int rsum_a_mask;
    int rsum_bits;
    int checksum_bytes;
    int seq_matches;
    int context;
    int skip;
    void* block_hashes;
    void* rsum_hash;
    void* bit_hash;
    int bit_hash_mask;
    int bit_hash_len;
    void* known;
    void* r0;
    void* r1;
    Stats* stats;
    int fd;
} RcksumState;

typedef struct RSum {
    int a;
    int b;
} RSum;

typedef struct BlockPair {
    int start;
    int fin;
} BlockPair;

typedef struct BlockRanges {
    void* ranges;
    int got_blocks;
} BlockRanges;

#define fs_KIND_OK (0)
#define fs_KIND_NOT_FOUND (1)
#define fs_KIND_PERMISSION_DENIED (2)
#define fs_KIND_EXISTS (3)
#define fs_KIND_CROSS_DEVICE (4)
#define fs_KIND_IO (5)
#define fs_KIND_INVALID (6)
#define fs_KIND_LOOP (7)
#define fs_KIND_NAME_TOO_LONG (8)
#define fs_KIND_NO_SPACE (9)
#define fs_KIND_IS_DIR (10)
#define fs_KIND_NOT_DIR (11)
#define fs_KIND_UNAVAILABLE (99)
#define json_KIND_OK (0)
#define json_KIND_PARSE_ERROR (1)
#define json_KIND_OUT_OF_MEMORY (2)
#define json_KIND_INVALID_INPUT (3)
// Forward declarations
const char* VERSION(void);
static _tuple_string_int string_strip_prefix(const char*, const char*);
static int64_t os_now_monotonic_ns(void);
static _tuple_int_string fs_mtime(const char*);
static int fs_exists(const char*);
static const char* fs_mkdir_p(const char*);
static const char* fs_write_binary(const char*, const char*, int);
static _tuple_string_int_string fs_read_binary(const char*);
static void* client_request(const char*, const char*);
static const char* client_set_header(void*, const char*, const char*);
static const char* client_set_body(void*, const char*, int, const char*);
static const char* client_set_timeout(void*, int64_t);
static void client_request_free(void*);
static _tuple_ptr_string client_send_request(void*);
static int client_response_status(void*);
static const char* client_response_body(void*);
static int client_response_body_length(void*);
static const char* client_response_url(void*);
static void client_response_free(void*);
static _tuple_string_int_string client_post_with_status(const char*, const char*, const char*);
static void* control_ctl_rs(Control*);
static int control_ctl_filelen(Control*);
static int control_ctl_blocks(Control*);
static int control_ctl_blocksize(Control*);
static const char* control_ctl_checksum(Control*);
static const char* control_ctl_filename(Control*);
static void* control_ctl_urls(Control*);
static int control_ctl_ok(Control*);
static const char* control_ctl_err(Control*);
static Control* control_fail(Control*, const char*);
static int control_be_decode(const char*, int, int, int);
static int control_is_pow2(int);
static Control* control_parse(const char*, int);
static const char* control_pad_md4(const char*, int, int, int);
static const char* control_substr(const char*, int, int);
static int control_index_byte(const char*, int, int, int);
static int control_find_sep(const char*);
static int control_parse_int(const char*);
static _tuple_int_int_int_int control_parse_hash_lengths(const char*);
static int control_parse_int_p(void*);
static int download_br_start(ByteRange*);
static int download_br_fin(ByteRange*);
static int download_br_count(void*);
static int download_br_at_start(void*, int);
static int download_br_at_fin(void*, int);
static State* download_new_state(void*, int);
static int download_status(State*);
static _tuple_int_int download_progress(State*);
static void download_submit_source(State*, const char*, int);
static void* download_needed_byte_ranges(State*);
static int download_submit_target_data(State*, int, const char*, int);
static const char* download_complete(State*);
static const char* download_lower(const char*);
static const char* download_stats_line(State*);
static const char* clientlib_basic_auth(const char*);
static _tuple_string_int_int_string clientlib_http_get_auth(const char*, const char*, const char*);
static _tuple_string_int_string clientlib_http_get_range_auth(const char*, int, int, const char*);
static const char* clientlib_range_header(int, int);
static const char* clientlib_copy_bytes(const char*, int);
static const char* clientlib_fetch_remaining(void*, const char*, const char*);
static const char* clientlib_concat3(const char*, const char*, const char*);
static const char* clientlib_int_str(int);
static int fileio_open_rw(const char*);
static int fileio_write_at(int, const char*, int, int);
static _tuple_string_int fileio_read_at(int, int, int);
static int fileio_truncate_to(int, int);
static int fileio_close_fd(int);
static int fileio_sync_fd(int);
static void* fileio_buf_alloc(int);
static const char* fileio_buf_alloc_str(int);
static int fileio_buf_get(void*, int);
static void fileio_buf_set(void*, int, int);
static void fileio_buf_or(void*, int, int);
static const char* fileio_buf_to_str(void*, int);
static const char* fileio_zero_prefix(const char*, int, int);
static const char* fileio_pad_block(const char*, int, int, int, int);
static const char* fileio_slice(const char*, int, int, int);
static const char* fileio_buf_as_string(void*);
static const char* fileio_rfc1123z(int);
static const char* fileio_dup16(void*);
static int rcksum_NO_BLOCK(void);
static int rcksum_BITHASH_BITS(void);
static void* rcksum_box_int(int);
static int rcksum_unbox_int(void*);
static HashEntry* rcksum_entry_at(RcksumState*, int);
static HashEntry* rcksum_new_entry(void);
static RcksumState* rcksum_new(int, int, int, int, int);
static void rcksum_set_target_fd(RcksumState*, int);
static void* rcksum_state_as_ptr(RcksumState*);
static void rcksum_set_target_fd_p(void*, int);
static int rcksum_blocks_todo_p(void*);
static int rcksum_submit_source_buffer_p(void*, const char*, int);
static void rcksum_submit_blocks_p(void*, const char*, int, int, int);
static void* rcksum_needed_block_ranges_p(void*);
static int rcksum_ranges_list_len(void*);
static int rcksum_ranges_list_start(void*, int);
static int rcksum_ranges_list_fin(void*, int);
static _tuple_string_int rcksum_read_known_data_p(void*, int, int);
static void rcksum_add_target_block_p(void*, int, int, int, const char*);
static int rcksum_stat_hash_hit_p(void*);
static int rcksum_stat_weak_hit_p(void*);
static int rcksum_stat_strong_hit_p(void*);
static int rcksum_stat_checksummed_p(void*);
static void rcksum_add_target_block(RcksumState*, int, int, int, const char*);
static int rcksum_blocks_todo(RcksumState*);
static int rcksum_calc_rhash(RcksumState*, int);
static const char* rcksum_hkey(int);
static int rcksum_rsum_hash_get(RcksumState*, int);
static void rcksum_rsum_hash_put(RcksumState*, int, int);
static void rcksum_rsum_hash_del(RcksumState*, int);
static void rcksum_build_hash(RcksumState*);
static void rcksum_remove_block_from_hash(RcksumState*, int);
static void* rcksum_needed_block_ranges(RcksumState*);
static int rcksum_prefix_eq(const char*, int, const char*, int, int);
static int rcksum_write_blocks(RcksumState*, const char*, int, int, int, int);
static void rcksum_submit_blocks(RcksumState*, const char*, int, int, int);
static int rcksum_match_block(RcksumState*, const char*, int, int);
static int rcksum_check_chain(RcksumState*, int, const char*, int, int);
static int rcksum_submit_source_data(RcksumState*, const char*, int, int);
static int rcksum_submit_source_buffer(RcksumState*, const char*, int);
static const char* rcksum_make_zeros(int);
static const char* rcksum_append_bytes(const char*, int, const char*, int);
static _tuple_string_int rcksum_read_known_data(RcksumState*, int, int);
static RSum* checksums_new_rsum(int, int);
static void* checksums_rsum_as_ptr(RSum*);
static int checksums_rsum_a(RSum*);
static int checksums_rsum_b(RSum*);
static RSum* checksums_calc_rsum_block(const char*, int, int, int);
static void checksums_update_rsum(RSum*, int, int, int);
static const char* checksums_calc_checksum(const char*, int);
static int checksums_byte_at(const char*, int, int);
static int checksums_calc_rhash_from_rsums(RSum*, RSum*, int, int);
static int checksums_log2(int);
static _tuple_string_string cryptography_sha1_hex(const char*, int);
static _tuple_string_string cryptography_base64_encode_padded(const char*, int);
static _tuple_string_int_string cryptography_md4_bytes(const char*, int);
static BlockPair* ranges_new_pair(int, int);
static int ranges_pair_start(BlockPair*);
static int ranges_pair_fin(BlockPair*);
static BlockRanges* ranges_new_ranges(void);
static void* ranges_ranges_as_ptr(BlockRanges*);
static int ranges_got_blocks(BlockRanges*);
static int ranges_range_start_at(BlockRanges*, int);
static int ranges_range_fin_at(BlockRanges*, int);
static BlockPair* ranges_pair_at(BlockRanges*, int);
static void ranges_insert_pair(BlockRanges*, int, BlockPair*);
static int ranges_range_before_block(BlockRanges*, int);
static void ranges_add_to_ranges(BlockRanges*, int);
static int ranges_contains(BlockRanges*, int);
static int ranges_next_contained_after(BlockRanges*, int);
static int ranges_list_len(void*);
static BlockPair* ranges_list_pair(void*, int);
static void* ranges_missing_blocks_between(BlockRanges*, int, int);
_tuple_string_int_string get_control(const char*, const char*, const char*, int);
const char* fetch_from_urls(void*, void*, const char*, const char*, int);
void feed_seed_if_exists(void*, const char*, int);
const char* creds_of(const char*);
const char* resolve(const char*, const char*);
int is_abs(const char*);
int last_slash(const char*);
const char* str_owned(const char*);

// Extern C function: string_new
void* string_new(const char*);

// Extern C function: string_from_cstr
void* string_from_cstr(const char*);

// Extern C function: string_from_literal
void* string_from_literal(const char*);

// Extern C function: string_new_with_length
void* string_new_with_length(const char*, int);

// Extern C function: string_empty
void* string_empty(void);

// Extern C function: string_retain
void string_retain(const char*);

// Extern C function: string_release
void string_release(const char*);

// Extern C function: string_free
void string_free(const char*);

// Extern C function: string_concat
const char* string_concat(const char*, const char*);

// Extern C function: string_concat_wrapped
void* string_concat_wrapped(const char*, const char*);

// Extern C function: string_length
int string_length(const char*);

// Extern C function: string_char_at
int string_char_at(const char*, int);

// Extern C function: string_equals
int string_equals(const char*, const char*);

// Extern C function: string_compare
int string_compare(const char*, const char*);

// Extern C function: string_starts_with
int string_starts_with(const char*, const char*);

// Extern C function: string_ends_with
int string_ends_with(const char*, const char*);

// Extern C function: string_contains
int string_contains(const char*, const char*);

// Extern C function: string_index_of
int string_index_of(const char*, const char*);

// Extern C function: string_index_of_from
int string_index_of_from(const char*, const char*, int);

// Extern C function: string_substring
const char* string_substring(const char*, int, int);

// Extern C function: string_substring_n
const char* string_substring_n(const char*, int, int, int);

// Extern C function: string_length_n
int string_length_n(const char*, int);

// Extern C function: string_char_at_n
int string_char_at_n(const char*, int, int);

// Extern C function: string_index_of_from_n
int string_index_of_from_n(const char*, int, const char*, int);

// Extern C function: string_from_char
void* string_from_char(int);

// Extern C function: string_to_upper
const char* string_to_upper(const char*);

// Extern C function: string_to_lower
const char* string_to_lower(const char*);

// Extern C function: string_trim
const char* string_trim(const char*);

// Extern C function: string_split
void* string_split(const char*, const char*);

// Extern C function: string_array_size
int string_array_size(void*);

// Extern C function: string_array_get
void* string_array_get(void*, int);

// Extern C function: string_array_free
void string_array_free(void*);

// Extern C function: string_split_to_seq
StringSeq* string_split_to_seq(const char*, const char*);

// Extern C function: string_seq_empty
StringSeq* string_seq_empty(void);

// Extern C function: string_seq_cons
StringSeq* string_seq_cons(const char*, StringSeq*);

// Extern C function: string_seq_head
const char* string_seq_head(StringSeq*);

// Extern C function: string_seq_tail
StringSeq* string_seq_tail(StringSeq*);

// Extern C function: string_seq_is_empty
int string_seq_is_empty(StringSeq*);

// Extern C function: string_seq_length
int string_seq_length(StringSeq*);

// Extern C function: string_seq_retain
StringSeq* string_seq_retain(StringSeq*);

// Extern C function: string_seq_free
void string_seq_free(StringSeq*);

// Extern C function: string_seq_from_array
StringSeq* string_seq_from_array(void*, int);

// Extern C function: string_seq_to_array
void* string_seq_to_array(StringSeq*);

// Extern C function: string_seq_reverse
StringSeq* string_seq_reverse(StringSeq*);

// Extern C function: string_seq_concat
StringSeq* string_seq_concat(StringSeq*, StringSeq*);

// Extern C function: string_seq_take
StringSeq* string_seq_take(StringSeq*, int);

// Extern C function: string_seq_drop
StringSeq* string_seq_drop(StringSeq*, int);

// Extern C function: string_to_cstr
const char* string_to_cstr(const char*);

// Extern C function: string_from_int
void* string_from_int(int);

// Extern C function: string_from_long
void* string_from_long(int64_t);

// Extern C function: string_from_float
void* string_from_float(double);

// Extern C function: string_from_int_radix
void* string_from_int_radix(int64_t, int);

// Extern C function: string_pad_start
void* string_pad_start(const char*, int, int);

// Extern C function: string_pad_end
void* string_pad_end(const char*, int, int);

// Extern C function: string_to_int_raw
int string_to_int_raw(const char*, void*);

// Extern C function: string_to_long_raw
int string_to_long_raw(const char*, void*);

// Extern C function: string_to_float_raw
int string_to_float_raw(const char*, void*);

// Extern C function: string_to_double_raw
int string_to_double_raw(const char*, void*);

// Extern C function: string_to_int_radix_raw
int string_to_int_radix_raw(const char*, int, void*);

// Extern C function: string_try_int
int string_try_int(const char*);

// Extern C function: string_get_int
int string_get_int(const char*);

// Extern C function: string_try_long
int string_try_long(const char*);

// Extern C function: string_get_long
int64_t string_get_long(const char*);

// Extern C function: string_try_float
int string_try_float(const char*);

// Extern C function: string_get_float
double string_get_float(const char*);

// Extern C function: string_try_double
int string_try_double(const char*);

// Extern C function: string_get_double
double string_get_double(const char*);

// Extern C function: string_try_int_radix
int string_try_int_radix(const char*, int);

// Extern C function: string_get_int_radix
int64_t string_get_int_radix(const char*, int);

// Extern C function: string_format_list
void* string_format_list(const char*, void*);

// Extern C function: string_glob_match_raw
int string_glob_match_raw(const char*, const char*, int);

// Extern C function: list_new
void* list_new(void);

// Extern C function: list_add_raw
int list_add_raw(void*, void*);

// Extern C function: list_add_string_owned
int list_add_string_owned(void*, void*);

// Extern C function: list_get_raw
void* list_get_raw(void*, int);

// Extern C function: list_set
void list_set(void*, int, void*);

// Extern C function: list_size
int list_size(void*);

// Extern C function: list_remove
void list_remove(void*, int);

// Extern C function: list_clear
void list_clear(void*);

// Extern C function: list_free
void list_free(void*);

// Extern C function: os_system
int os_system(const char*);

// Extern C function: os_exec_raw
const char* os_exec_raw(const char*);

// Extern C function: os_run
int os_run(const char*, void*, void*);

// Extern C function: os_run_capture_raw
const char* os_run_capture_raw(const char*, void*, void*);

// Extern C function: os_run_capture_status_raw
_tuple_string_int_string os_run_capture_status_raw(const char*, void*, void*);

// Extern C function: os_run_pipe_raw
_tuple_int_int_string os_run_pipe_raw(const char*, void*, void*);

// Extern C function: os_wait_pid_raw
_tuple_int_string os_wait_pid_raw(int);

// Extern C function: os_run_pipe_drain_and_wait_raw
_tuple_string_int_string os_run_pipe_drain_and_wait_raw(const char*, void*, void*);

// Extern C function: os_getenv
const char* os_getenv(const char*);

// Extern C function: io_setenv_raw
int io_setenv_raw(const char*, const char*);

// Extern C function: io_unsetenv_raw
int io_unsetenv_raw(const char*);

// Extern C function: os_which
const char* os_which(const char*);

// Extern C function: aether_args_count
int aether_args_count(void);

// Extern C function: aether_args_get
const char* aether_args_get(int);

// Extern C function: aether_argv0
const char* aether_argv0(void);

// Extern C function: aether_argv_raw
void* aether_argv_raw(void);

// Extern C function: aether_args_seal
void aether_args_seal(void);

// Extern C function: aether_args_sealed
int aether_args_sealed(void);

// Extern C function: os_execv
int os_execv(const char*, void*);

// Extern C function: string_concat
const char* string_concat(const char*, const char*);

// Extern C function: os_now_utc_iso8601_raw
const char* os_now_utc_iso8601_raw(void);

// Extern C function: os_now_local_fill_raw
void os_now_local_fill_raw(void*);

// Extern C function: os_now_local_iso8601_raw
const char* os_now_local_iso8601_raw(void);

// Extern C function: malloc (libc-provided, declaration skipped)
// Extern C function: os_platform_raw
const char* os_platform_raw(void);

// Extern C function: os_getpid_raw
int os_getpid_raw(void);

// Extern C function: exit (libc-provided, declaration skipped)
// Extern C function: os_wall_seconds_raw
int64_t os_wall_seconds_raw(void);

// Extern C function: os_wall_micros_raw
int os_wall_micros_raw(void);

// Extern C function: os_now_monotonic_ms_raw
int64_t os_now_monotonic_ms_raw(void);

// Extern C function: os_now_monotonic_ns_raw
int64_t os_now_monotonic_ns_raw(void);

// Extern C function: os_now_unix_ms_raw
int64_t os_now_unix_ms_raw(void);

// Extern C function: file_open_raw
void* file_open_raw(const char*, const char*);

// Extern C function: file_close
int file_close(void*);

// Extern C function: file_read_all_raw
const char* file_read_all_raw(void*);

// Extern C function: file_write_raw
int file_write_raw(void*, const char*, int);

// Extern C function: file_exists
int file_exists(const char*);

// Extern C function: fs_path_exists
int fs_path_exists(const char*);

// Extern C function: file_delete_raw
int file_delete_raw(const char*);

// Extern C function: file_size_raw
int file_size_raw(const char*);

// Extern C function: file_mtime
int file_mtime(const char*);

// Extern C function: file_mtime_raw
int file_mtime_raw(const char*);

// Extern C function: dir_exists
int dir_exists(const char*);

// Extern C function: dir_create_raw
int dir_create_raw(const char*);

// Extern C function: dir_create_mode_raw
int dir_create_mode_raw(const char*, int);

// Extern C function: dir_delete_raw
int dir_delete_raw(const char*);

// Extern C function: dir_list_raw
void* dir_list_raw(const char*);

// Extern C function: fs_mkdir_p_raw
int fs_mkdir_p_raw(const char*);

// Extern C function: fs_symlink_raw
int fs_symlink_raw(const char*, const char*);

// Extern C function: fs_readlink_raw
const char* fs_readlink_raw(const char*);

// Extern C function: fs_is_symlink
int fs_is_symlink(const char*);

// Extern C function: fs_unlink_raw
int fs_unlink_raw(const char*);

// Extern C function: fs_write_binary_raw
int fs_write_binary_raw(const char*, const char*, int);

// Extern C function: fs_write_atomic_raw
int fs_write_atomic_raw(const char*, const char*, int);

// Extern C function: fs_rename_raw
int fs_rename_raw(const char*, const char*);

// Extern C function: fs_try_stat
int fs_try_stat(const char*);

// Extern C function: fs_get_stat_kind
int fs_get_stat_kind(void);

// Extern C function: fs_get_stat_size
int fs_get_stat_size(void);

// Extern C function: fs_get_stat_mtime
int fs_get_stat_mtime(void);

// Extern C function: fs_try_read_binary
int fs_try_read_binary(const char*);

// Extern C function: fs_get_read_binary
const char* fs_get_read_binary(void);

// Extern C function: fs_get_read_binary_length
int fs_get_read_binary_length(void);

// Extern C function: fs_release_read_binary
void fs_release_read_binary(void);

// Extern C function: fs_read_binary_tuple
_tuple_string_int_string fs_read_binary_tuple(const char*);

// Extern C function: fs_copy_raw
_tuple_int_int_string fs_copy_raw(const char*, const char*);

// Extern C function: fs_move_raw
_tuple_int_int_string fs_move_raw(const char*, const char*);

// Extern C function: fs_realpath_raw
_tuple_string_int_string fs_realpath_raw(const char*);

// Extern C function: fs_chmod_raw
_tuple_int_int_string fs_chmod_raw(const char*, int);

// Extern C function: dir_list_count
int dir_list_count(void*);

// Extern C function: dir_list_get
const char* dir_list_get(void*, int);

// Extern C function: dir_list_free
void dir_list_free(void*);

// Extern C function: path_join
const char* path_join(const char*, const char*);

// Extern C function: path_dirname
const char* path_dirname(const char*);

// Extern C function: path_basename
const char* path_basename(const char*);

// Extern C function: path_extension
const char* path_extension(const char*);

// Extern C function: path_is_absolute
int path_is_absolute(const char*);

// Extern C function: path_clean
const char* path_clean(const char*);

// Extern C function: path_is_within_base
int path_is_within_base(const char*, const char*);

// Extern C function: path_rel
const char* path_rel(const char*, const char*);

// Extern C function: fs_pwrite_raw
int64_t fs_pwrite_raw(void*, const char*, int, int64_t);

// Extern C function: fs_pread_raw
int fs_pread_raw(void*, int, int64_t);

// Extern C function: fs_get_pread
const char* fs_get_pread(void);

// Extern C function: fs_get_pread_length
int fs_get_pread_length(void);

// Extern C function: fs_release_pread
void fs_release_pread(void);

// Extern C function: fs_ftruncate_raw
const char* fs_ftruncate_raw(void*, int64_t);

// Extern C function: fs_fsync_raw
const char* fs_fsync_raw(void*);

// Extern C function: fs_glob_raw
void* fs_glob_raw(const char*);

// Extern C function: fs_glob_multi_raw
void* fs_glob_multi_raw(void*);

// Extern C function: string_concat
const char* string_concat(const char*, const char*);

// Extern C function: string_length
int string_length(const char*);

// Extern C function: string_new_with_length
void* string_new_with_length(const char*, int);

// Extern C function: http_request_raw
void* http_request_raw(const char*, const char*);

// Extern C function: http_request_set_header_raw
int http_request_set_header_raw(void*, const char*, const char*);

// Extern C function: http_request_set_body_raw
int http_request_set_body_raw(void*, const char*, int, const char*);

// Extern C function: http_request_set_timeout_raw
int http_request_set_timeout_raw(void*, int);

// Extern C function: http_request_set_timeout_ns_raw
int http_request_set_timeout_ns_raw(void*, int64_t);

// Extern C function: http_request_set_follow_redirects_raw
int http_request_set_follow_redirects_raw(void*, int);

// Extern C function: http_request_free_raw
void http_request_free_raw(void*);

// Extern C function: http_send_raw
void* http_send_raw(void*);

// Extern C function: http_response_status
int http_response_status(void*);

// Extern C function: http_response_body
const char* http_response_body(void*);

// Extern C function: http_response_body_length
int http_response_body_length(void*);

// Extern C function: http_response_headers
const char* http_response_headers(void*);

// Extern C function: http_response_error
const char* http_response_error(void*);

// Extern C function: http_response_redirect_error_raw
const char* http_response_redirect_error_raw(void*);

// Extern C function: http_response_header_raw
const char* http_response_header_raw(void*, const char*);

// Extern C function: http_response_effective_url_raw
const char* http_response_effective_url_raw(void*);

// Extern C function: http_response_free
void http_response_free(void*);

// Extern C function: string_concat
const char* string_concat(const char*, const char*);

// Extern C function: string_length
int string_length(const char*);

// Extern C function: malloc (libc-provided, declaration skipped)
// Extern C function: malloc (libc-provided, declaration skipped)
// Extern C function: malloc (libc-provided, declaration skipped)
// Extern C function: zsync_io_open_rw_trunc
int zsync_io_open_rw_trunc(const char*);

// Extern C function: zsync_io_open_ro
int zsync_io_open_ro(const char*);

// Extern C function: zsync_io_pwrite
int64_t zsync_io_pwrite(int, const char*, int64_t, int64_t);

// Extern C function: zsync_io_pread_alloc
const char* zsync_io_pread_alloc(int, int64_t, int64_t);

// Extern C function: zsync_io_last_read_len
int64_t zsync_io_last_read_len(void);

// Extern C function: zsync_io_ftruncate
int zsync_io_ftruncate(int, int64_t);

// Extern C function: zsync_io_close
int zsync_io_close(int);

// Extern C function: zsync_io_fsync
int zsync_io_fsync(int);

// Extern C function: zsync_buf_alloc
void* zsync_buf_alloc(int64_t);

// Extern C function: zsync_buf_alloc_str
const char* zsync_buf_alloc_str(int64_t);

// Extern C function: zsync_buf_get
int zsync_buf_get(void*, int64_t);

// Extern C function: zsync_buf_set
void zsync_buf_set(void*, int64_t, int);

// Extern C function: zsync_buf_or
void zsync_buf_or(void*, int64_t, int);

// Extern C function: zsync_buf_free
void zsync_buf_free(void*);

// Extern C function: zsync_buf_identity
const char* zsync_buf_identity(void*);

// Extern C function: zsync_rfc1123z
const char* zsync_rfc1123z(int64_t);

// Extern C function: zsync_parse_rfc1123
int64_t zsync_parse_rfc1123(const char*);

// Extern C function: zsync_dup16
const char* zsync_dup16(void*);

// Extern C function: json_parse_raw
void* json_parse_raw(const char*);

// Extern C function: json_last_error
const char* json_last_error(void);

// Extern C function: json_last_error_kind
int json_last_error_kind(void);

// Extern C function: json_last_error_line
int json_last_error_line(void);

// Extern C function: json_last_error_col
int json_last_error_col(void);

// Extern C function: json_stringify_raw
const char* json_stringify_raw(void*);

// Extern C function: json_free
void json_free(void*);

// Extern C function: json_type
int json_type(void*);

// Extern C function: json_is_null
int json_is_null(void*);

// Extern C function: json_get_bool
int json_get_bool(void*);

// Extern C function: json_get_number
double json_get_number(void*);

// Extern C function: json_get_int
int json_get_int(void*);

// Extern C function: json_get_string_raw
const char* json_get_string_raw(void*);

// Extern C function: json_object_get_raw
void* json_object_get_raw(void*, const char*);

// Extern C function: json_object_set_raw
int json_object_set_raw(void*, const char*, void*);

// Extern C function: json_object_has
int json_object_has(void*, const char*);

// Extern C function: json_object_size_raw
int json_object_size_raw(void*);

// Extern C function: json_object_key_at
const char* json_object_key_at(void*, int);

// Extern C function: json_object_value_at
void* json_object_value_at(void*, int);

// Extern C function: json_array_get_raw
void* json_array_get_raw(void*, int);

// Extern C function: json_array_add_raw
int json_array_add_raw(void*, void*);

// Extern C function: json_array_size
int json_array_size(void*);

// Extern C function: json_create_null
void* json_create_null(void);

// Extern C function: json_create_bool
void* json_create_bool(int);

// Extern C function: json_create_number
void* json_create_number(double);

// Extern C function: json_create_string
void* json_create_string(const char*);

// Extern C function: json_create_array
void* json_create_array(void);

// Extern C function: json_create_object
void* json_create_object(void);

// Extern C function: string_concat
const char* string_concat(const char*, const char*);

// Extern C function: malloc (libc-provided, declaration skipped)
// Extern C function: malloc (libc-provided, declaration skipped)
// Extern C function: free (libc-provided, declaration skipped)
// Extern C function: cryptography_sha1_hex_raw
const char* cryptography_sha1_hex_raw(const char*, int);

// Extern C function: cryptography_sha256_hex_raw
const char* cryptography_sha256_hex_raw(const char*, int);

// Extern C function: cryptography_hash_hex_raw
const char* cryptography_hash_hex_raw(const char*, const char*, int);

// Extern C function: cryptography_hash_supported
int cryptography_hash_supported(const char*);

// Extern C function: cryptography_base64_encode_raw
const char* cryptography_base64_encode_raw(const char*, int);

// Extern C function: cryptography_base64_encode_padded_raw
const char* cryptography_base64_encode_padded_raw(const char*, int);

// Extern C function: cryptography_base64_decode_raw
int cryptography_base64_decode_raw(const char*);

// Extern C function: cryptography_get_base64_decode
const char* cryptography_get_base64_decode(void);

// Extern C function: cryptography_get_base64_decode_length
int cryptography_get_base64_decode_length(void);

// Extern C function: cryptography_release_base64_decode
void cryptography_release_base64_decode(void);

// Extern C function: cryptography_md4_hex_raw
const char* cryptography_md4_hex_raw(const char*, int);

// Extern C function: cryptography_md5_hex_raw
const char* cryptography_md5_hex_raw(const char*, int);

// Extern C function: cryptography_hmac_sha256_hex_raw
const char* cryptography_hmac_sha256_hex_raw(const char*, int, const char*, int);

// Extern C function: cryptography_hmac_sha256_bytes_raw
int cryptography_hmac_sha256_bytes_raw(const char*, int, const char*, int);

// Extern C function: cryptography_md4_bytes_raw
int cryptography_md4_bytes_raw(const char*, int);

// Extern C function: cryptography_md5_bytes_raw
int cryptography_md5_bytes_raw(const char*, int);

// Extern C function: cryptography_sha1_bytes_raw
int cryptography_sha1_bytes_raw(const char*, int);

// Extern C function: cryptography_sha256_bytes_raw
int cryptography_sha256_bytes_raw(const char*, int);

// Extern C function: cryptography_hash_bytes_raw
int cryptography_hash_bytes_raw(const char*, const char*, int);

// Extern C function: cryptography_get_binary_digest
const char* cryptography_get_binary_digest(void);

// Extern C function: cryptography_get_binary_digest_length
int cryptography_get_binary_digest_length(void);

// Extern C function: cryptography_release_binary_digest
void cryptography_release_binary_digest(void);

// Extern C function: cryptography_random_bytes_raw
int cryptography_random_bytes_raw(int);

// Extern C function: cryptography_get_random_bytes
const char* cryptography_get_random_bytes(void);

// Extern C function: cryptography_get_random_bytes_length
int cryptography_get_random_bytes_length(void);

// Extern C function: cryptography_release_random_bytes
void cryptography_release_random_bytes(void);

// Extern C function: string_new_with_length
void* string_new_with_length(const char*, int);

// Extern C function: map_new
void* map_new(void);

// Extern C function: map_put_raw
int map_put_raw(void*, const char*, void*);

// Extern C function: map_put_string_owned
int map_put_string_owned(void*, const char*, void*);

// Extern C function: map_get_raw
void* map_get_raw(void*, const char*);

// Extern C function: map_has
int map_has(void*, const char*);

// Extern C function: map_remove
void map_remove(void*, const char*);

// Extern C function: map_size
int map_size(void*);

// Extern C function: map_clear
void map_clear(void*);

// Extern C function: map_free
void map_free(void*);

// Extern C function: map_keys_raw
void* map_keys_raw(void*);

// Extern C function: map_keys_free
void map_keys_free(void*);

// Extern C function: malloc (libc-provided, declaration skipped)

// Import: std.string
// Import: std.list
// Import: std.os
// Import: std.fs
// Import: std.http.client
// Import: zsync.control
// Import: zsync.download
// Import: cmd.clientlib
// Import: rcksum.fileio
#line 22 "cmd/zsync.ae"
const char* VERSION(void) {
#line 23 "cmd/zsync.ae"
    return "0.7.1";
}

#line 363 "/home/paul/scm/aether/build/../std/string/module.ae"
static _tuple_string_int string_strip_prefix(const char* s, const char* prefix) {
if (string_starts_with(s, prefix) != 1) {
        {
#line 365 "/home/paul/scm/aether/build/../std/string/module.ae"
            return (_tuple_string_int){aether_uniform_heap_str((const char*)(s), 0), 0};
        }
    }
#line 367 "/home/paul/scm/aether/build/../std/string/module.ae"
int prefix_len = string_length(prefix);
#line 368 "/home/paul/scm/aether/build/../std/string/module.ae"
int s_len = string_length(s);
#line 369 "/home/paul/scm/aether/build/../std/string/module.ae"
    return (_tuple_string_int){aether_uniform_heap_str((const char*)(string_substring(s, prefix_len, s_len)), 1), 1};
}

#line 442 "/home/paul/scm/aether/build/../std/os/module.ae"
static int64_t os_now_monotonic_ns(void) {
#line 443 "/home/paul/scm/aether/build/../std/os/module.ae"
    return os_now_monotonic_ns_raw();
}

#line 331 "/home/paul/scm/aether/build/../std/fs/module.ae"
static _tuple_int_string fs_mtime(const char* path) {
#line 332 "/home/paul/scm/aether/build/../std/fs/module.ae"
int m = file_mtime_raw(aether_string_data(path));
if (m < 0) {
        {
#line 334 "/home/paul/scm/aether/build/../std/fs/module.ae"
            return (_tuple_int_string){0, "cannot stat file"};
        }
    }
#line 336 "/home/paul/scm/aether/build/../std/fs/module.ae"
    return (_tuple_int_string){m, ""};
}

#line 345 "/home/paul/scm/aether/build/../std/fs/module.ae"
static int fs_exists(const char* path) {
#line 346 "/home/paul/scm/aether/build/../std/fs/module.ae"
    return fs_path_exists(aether_string_data(path));
}

#line 384 "/home/paul/scm/aether/build/../std/fs/module.ae"
static const char* fs_mkdir_p(const char* path) {
#line 385 "/home/paul/scm/aether/build/../std/fs/module.ae"
int ok = fs_mkdir_p_raw(aether_string_data(path));
if (ok == 0) {
        {
#line 387 "/home/paul/scm/aether/build/../std/fs/module.ae"
            return "cannot mkdir -p";
        }
    }
#line 389 "/home/paul/scm/aether/build/../std/fs/module.ae"
    return "";
}

#line 468 "/home/paul/scm/aether/build/../std/fs/module.ae"
static const char* fs_write_binary(const char* path, const char* data, int length) {
#line 469 "/home/paul/scm/aether/build/../std/fs/module.ae"
int ok = fs_write_binary_raw(aether_string_data(path), aether_string_data(data), length);
if (ok == 0) {
        {
#line 471 "/home/paul/scm/aether/build/../std/fs/module.ae"
            return "binary write failed";
        }
    }
#line 473 "/home/paul/scm/aether/build/../std/fs/module.ae"
    return "";
}

#line 534 "/home/paul/scm/aether/build/../std/fs/module.ae"
static _tuple_string_int_string fs_read_binary(const char* path) {
#line 540 "/home/paul/scm/aether/build/../std/fs/module.ae"
    return fs_read_binary_tuple(aether_string_data(path));
}

#line 101 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static void* client_request(const char* method, const char* url) {
#line 102 "/home/paul/scm/aether/build/../std/http/client/module.ae"
    return http_request_raw(aether_string_data(method), aether_string_data(url));
}

#line 107 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static const char* client_set_header(void* req, const char* name, const char* value) {
#line 108 "/home/paul/scm/aether/build/../std/http/client/module.ae"
int rc = http_request_set_header_raw(req, aether_string_data(name), aether_string_data(value));
if (rc != 0) {
        {
#line 109 "/home/paul/scm/aether/build/../std/http/client/module.ae"
            return "set_header failed";
        }
    }
#line 110 "/home/paul/scm/aether/build/../std/http/client/module.ae"
    return "";
}

#line 118 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static const char* client_set_body(void* req, const char* body, int length, const char* content_type) {
#line 119 "/home/paul/scm/aether/build/../std/http/client/module.ae"
int rc = http_request_set_body_raw(req, aether_string_data(body), length, aether_string_data(content_type));
if (rc != 0) {
        {
#line 120 "/home/paul/scm/aether/build/../std/http/client/module.ae"
            return "set_body failed";
        }
    }
#line 121 "/home/paul/scm/aether/build/../std/http/client/module.ae"
    return "";
}

#line 127 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static const char* client_set_timeout(void* req, int64_t timeout) {
#line 128 "/home/paul/scm/aether/build/../std/http/client/module.ae"
int rc = http_request_set_timeout_ns_raw(req, timeout);
if (rc != 0) {
        {
#line 129 "/home/paul/scm/aether/build/../std/http/client/module.ae"
            return "set_timeout failed";
        }
    }
#line 130 "/home/paul/scm/aether/build/../std/http/client/module.ae"
    return "";
}

#line 157 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static void client_request_free(void* req) {
http_request_free_raw(req);
}

#line 168 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static _tuple_ptr_string client_send_request(void* req) {
    int _heap_err = 0; (void)_heap_err;
    const char* err = NULL;
    int _heap_err_copy = 0; (void)_heap_err_copy;
    const char* err_copy = NULL;
#line 169 "/home/paul/scm/aether/build/../std/http/client/module.ae"
void* resp = http_send_raw(req);
if (resp == NULL) {
        {
#line 171 "/home/paul/scm/aether/build/../std/http/client/module.ae"
            _tuple_ptr_string _builder_ret = (_tuple_ptr_string){NULL, aether_uniform_heap_str((const char*)("out of memory"), 0)};
            /* deferred */ if (_heap_err) { aether_heap_str_free(err); err = NULL; _heap_err = 0; }
            return _builder_ret;
        }
    }
#line 173 "/home/paul/scm/aether/build/../std/http/client/module.ae"
{ const char* _tmp_old = err; err = http_response_error(resp); if (_heap_err) aether_heap_str_free(_tmp_old); _heap_err = 0; }
if (strcmp(_aether_safe_str(err), _aether_safe_str("")) != 0) {
        {
#line 178 "/home/paul/scm/aether/build/../std/http/client/module.ae"
{ const char* _tmp_old = err_copy; err_copy = string_concat(err, ""); if (_heap_err_copy) aether_heap_str_free(_tmp_old); _heap_err_copy = 1; }
#line 179 "/home/paul/scm/aether/build/../std/http/client/module.ae"
http_response_free(resp);
#line 180 "/home/paul/scm/aether/build/../std/http/client/module.ae"
            _tuple_ptr_string _builder_ret = (_tuple_ptr_string){NULL, aether_uniform_heap_str((const char*)(err_copy), _heap_err_copy)};
            /* deferred */ if (_heap_err) { aether_heap_str_free(err); err = NULL; _heap_err = 0; }
            return _builder_ret;
        }
    }
#line 182 "/home/paul/scm/aether/build/../std/http/client/module.ae"
    _tuple_ptr_string _builder_ret = (_tuple_ptr_string){resp, aether_uniform_heap_str((const char*)(""), 0)};
    /* deferred */ if (_heap_err) { aether_heap_str_free(err); err = NULL; _heap_err = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_err) { aether_heap_str_free(err); err = NULL; _heap_err = 0; }
}

#line 188 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static int client_response_status(void* resp) {
    return http_response_status(resp);
}

#line 189 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static const char* client_response_body(void* resp) {
    return http_response_body(resp);
}

#line 197 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static int client_response_body_length(void* resp) {
#line 198 "/home/paul/scm/aether/build/../std/http/client/module.ae"
    return http_response_body_length(resp);
}

#line 246 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static const char* client_response_url(void* resp) {
    return http_response_effective_url_raw(resp);
}

#line 249 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static void client_response_free(void* resp) {
http_response_free(resp);
}

#line 298 "/home/paul/scm/aether/build/../std/http/client/module.ae"
static _tuple_string_int_string client_post_with_status(const char* url, const char* body, const char* content_type) {
    int _heap_err = 0; (void)_heap_err;
    const char* err = NULL;
    int _heap_rbody = 0; (void)_heap_rbody;
    const char* rbody = NULL;
    int _heap_rbody_copy = 0; (void)_heap_rbody_copy;
    const char* rbody_copy = NULL;
#line 299 "/home/paul/scm/aether/build/../std/http/client/module.ae"
void* req = client_request("POST", url);
if (req == NULL) {
        {
#line 300 "/home/paul/scm/aether/build/../std/http/client/module.ae"
            _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(""), 0), 0, aether_uniform_heap_str((const char*)("request build failed"), 0)};
            /* deferred */ if (_heap_rbody) { aether_heap_str_free(rbody); rbody = NULL; _heap_rbody = 0; }
            return _builder_ret;
        }
    }
#line 301 "/home/paul/scm/aether/build/../std/http/client/module.ae"
client_set_timeout(req, 30000000000LL);
#line 302 "/home/paul/scm/aether/build/../std/http/client/module.ae"
int body_len = string_length(body);
#line 303 "/home/paul/scm/aether/build/../std/http/client/module.ae"
client_set_body(req, body, body_len, content_type);
#line 304 "/home/paul/scm/aether/build/../std/http/client/module.ae"
    _tuple_ptr_string _tup0 = client_send_request(req);
    void* resp = _tup0._0;
    { const char* _tmp_old = err; err = _tup0._1; if (_heap_err) aether_heap_str_free(_tmp_old); _heap_err = 1; }
#line 305 "/home/paul/scm/aether/build/../std/http/client/module.ae"
client_request_free(req);
if (strcmp(_aether_safe_str(err), _aether_safe_str("")) != 0) {
        {
#line 306 "/home/paul/scm/aether/build/../std/http/client/module.ae"
            _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(""), 0), 0, aether_uniform_heap_str((const char*)(err), _heap_err)};
            /* deferred */ if (_heap_rbody) { aether_heap_str_free(rbody); rbody = NULL; _heap_rbody = 0; }
            return _builder_ret;
        }
    }
#line 307 "/home/paul/scm/aether/build/../std/http/client/module.ae"
{ const char* _tmp_old = rbody; rbody = client_response_body(resp); if (_heap_rbody) aether_heap_str_free(_tmp_old); _heap_rbody = 0; }
#line 308 "/home/paul/scm/aether/build/../std/http/client/module.ae"
int status = client_response_status(resp);
#line 309 "/home/paul/scm/aether/build/../std/http/client/module.ae"
{ const char* _tmp_old = rbody_copy; rbody_copy = string_concat(rbody, ""); if (_heap_rbody_copy) aether_heap_str_free(_tmp_old); _heap_rbody_copy = 1; }
#line 310 "/home/paul/scm/aether/build/../std/http/client/module.ae"
client_response_free(resp);
#line 311 "/home/paul/scm/aether/build/../std/http/client/module.ae"
    _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(rbody_copy), _heap_rbody_copy), status, aether_uniform_heap_str((const char*)(""), 0)};
    /* deferred */ if (_heap_rbody) { aether_heap_str_free(rbody); rbody = NULL; _heap_rbody = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_rbody) { aether_heap_str_free(rbody); rbody = NULL; _heap_rbody = 0; }
}

#line 31 "zsync/control.ae"
static void* control_ctl_rs(Control* c) {
    return c->rs;
}

#line 32 "zsync/control.ae"
static int control_ctl_filelen(Control* c) {
    return c->filelen;
}

#line 33 "zsync/control.ae"
static int control_ctl_blocks(Control* c) {
    return c->blocks;
}

#line 34 "zsync/control.ae"
static int control_ctl_blocksize(Control* c) {
    return c->blocksize;
}

#line 35 "zsync/control.ae"
static const char* control_ctl_checksum(Control* c) {
    return c->checksum;
}

#line 36 "zsync/control.ae"
static const char* control_ctl_filename(Control* c) {
    return c->filename;
}

#line 38 "zsync/control.ae"
static void* control_ctl_urls(Control* c) {
    return c->urls;
}

#line 39 "zsync/control.ae"
static int control_ctl_ok(Control* c) {
    return c->ok;
}

#line 40 "zsync/control.ae"
static const char* control_ctl_err(Control* c) {
    return c->err;
}

#line 42 "zsync/control.ae"
static Control* control_fail(Control* c, const char* msg) {
#line 43 "zsync/control.ae"
(c->ok = 0);
#line 44 "zsync/control.ae"
(c->err = msg);
#line 45 "zsync/control.ae"
    return c;
}

#line 49 "zsync/control.ae"
static int control_be_decode(const char* data, int data_len, int off, int n) {
#line 50 "zsync/control.ae"
int v = 0;
#line 51 "zsync/control.ae"
int i = 0;
while (i < n) {
        {
#line 53 "zsync/control.ae"
v = ((v << 8) | (string_char_at_n(data, data_len, (off + i)) & 0xff));
#line 54 "zsync/control.ae"
i = (i + 1);
        }
    }
#line 56 "zsync/control.ae"
    return v;
}

#line 60 "zsync/control.ae"
static int control_is_pow2(int x) {
if (x == 0) {
        {
#line 62 "zsync/control.ae"
            return 0;
        }
    }
if ((x & (x - 1)) == 0) {
        {
#line 65 "zsync/control.ae"
            return 1;
        }
    }
#line 67 "zsync/control.ae"
    return 0;
}

#line 71 "zsync/control.ae"
static Control* control_parse(const char* data, int data_len) {
    int _heap_line = 0; (void)_heap_line;
    const char* line = NULL;
    int _heap_key = 0; (void)_heap_key;
    const char* key = NULL;
    int _heap_value = 0; (void)_heap_value;
    const char* value = NULL;
    int _heap_md = 0; (void)_heap_md;
    const char* md = NULL;
#line 72 "zsync/control.ae"
void* raw = malloc(96);
#line 73 "zsync/control.ae"
Control* c = ((Control*)(raw));
#line 74 "zsync/control.ae"
(c->filelen = 0);
#line 75 "zsync/control.ae"
(c->blocks = 0);
#line 76 "zsync/control.ae"
(c->blocksize = 0);
#line 77 "zsync/control.ae"
(c->checksum = "");
#line 78 "zsync/control.ae"
(c->checksum_method = "");
#line 79 "zsync/control.ae"
(c->filename = "");
#line 80 "zsync/control.ae"
(c->mtime = "");
#line 81 "zsync/control.ae"
(c->urls = list_new());
#line 82 "zsync/control.ae"
(c->ok = 0);
#line 83 "zsync/control.ae"
(c->err = "");
#line 85 "zsync/control.ae"
int checksum_bytes = 16;
#line 86 "zsync/control.ae"
int rsum_bytes = 4;
#line 87 "zsync/control.ae"
int seq_matches = 1;
#line 90 "zsync/control.ae"
int pos = 0;
#line 91 "zsync/control.ae"
int header_end = 0;
    int nl;
    int line_len;
    int sep;
while (pos < data_len) {
        {
#line 94 "zsync/control.ae"
nl = control_index_byte(data, data_len, pos, 10);
if (nl < 0) {
                {
#line 96 "zsync/control.ae"
                    Control* _builder_ret = control_fail(c, "control file: unterminated header");
                    /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                    /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                    /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                    return _builder_ret;
                }
            }
#line 98 "zsync/control.ae"
line_len = (nl - pos);
if (line_len == 0) {
                {
#line 100 "zsync/control.ae"
header_end = (nl + 1);
#line 101 "zsync/control.ae"
pos = data_len;
                }
            } else {
                {
#line 103 "zsync/control.ae"
{ const char* _tmp_old = line; line = control_substr(data, pos, nl); if (_heap_line) aether_heap_str_free(_tmp_old); _heap_line = 1; }
#line 105 "zsync/control.ae"
sep = control_find_sep(line);
if (sep < 0) {
                        {
#line 107 "zsync/control.ae"
                            Control* _builder_ret = control_fail(c, "bad line");
                            /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                            /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                            /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                            return _builder_ret;
                        }
                    }
#line 109 "zsync/control.ae"
{ const char* _tmp_old = key; key = string_substring(line, 0, sep); if (_heap_key) aether_heap_str_free(_tmp_old); _heap_key = 1; }
#line 110 "zsync/control.ae"
value = string_substring(line, (sep + 2), string_length(line));
if (string_equals(key, "zsync") == 1) {
                        {
if (string_equals(value, "0.0.4") == 1) {
                                {
#line 114 "zsync/control.ae"
                                    Control* _builder_ret = control_fail(c, "not compatible with zsync 0.0.4");
                                    /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                    /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                    /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                    return _builder_ret;
                                }
                            }
                        }
                    } else {
if (string_equals(key, "Min-Version") == 1) {
                            {
                            }
                        } else {
if (string_equals(key, "Length") == 1) {
                                {
#line 119 "zsync/control.ae"
(c->filelen = control_parse_int(value));
                                }
                            } else {
if (string_equals(key, "Filename") == 1) {
                                    {
#line 121 "zsync/control.ae"
(c->filename = value);
                                    }
                                } else {
if (string_equals(key, "URL") == 1) {
                                        {
#line 123 "zsync/control.ae"
list_add_string_owned(c->urls, (void*)value);
                                        }
                                    } else {
if (string_equals(key, "Blocksize") == 1) {
                                            {
#line 125 "zsync/control.ae"
(c->blocksize = control_parse_int(value));
if (control_is_pow2(c->blocksize) == 0) {
                                                    {
#line 127 "zsync/control.ae"
                                                        Control* _builder_ret = control_fail(c, "nonsensical blocksize");
                                                        /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                                        /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                                        /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                                        return _builder_ret;
                                                    }
                                                }
                                            }
                                        } else {
if (string_equals(key, "Hash-Lengths") == 1) {
                                                {
#line 130 "zsync/control.ae"
                                                    _tuple_int_int_int_int _tup1 = control_parse_hash_lengths(value);
                                                    int sm = _tup1._0;
                                                    int rb = _tup1._1;
                                                    int cb = _tup1._2;
                                                    int ok = _tup1._3;
if (ok == 0) {
                                                        {
#line 132 "zsync/control.ae"
                                                            Control* _builder_ret = control_fail(c, "bad hash lengths");
                                                            /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                                            /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                                            /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                                            return _builder_ret;
                                                        }
                                                    }
#line 134 "zsync/control.ae"
seq_matches = sm;
#line 135 "zsync/control.ae"
rsum_bytes = rb;
#line 136 "zsync/control.ae"
checksum_bytes = cb;
if (rsum_bytes < 1) {
                                                        {
#line 138 "zsync/control.ae"
                                                            Control* _builder_ret = control_fail(c, "nonsensical hash lengths");
                                                            /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                                            /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                                            /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                                            return _builder_ret;
                                                        }
                                                    }
if (rsum_bytes > 4) {
                                                        {
#line 141 "zsync/control.ae"
                                                            Control* _builder_ret = control_fail(c, "nonsensical hash lengths");
                                                            /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                                            /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                                            /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                                            return _builder_ret;
                                                        }
                                                    }
if (checksum_bytes < 3) {
                                                        {
#line 144 "zsync/control.ae"
                                                            Control* _builder_ret = control_fail(c, "nonsensical hash lengths");
                                                            /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                                            /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                                            /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                                            return _builder_ret;
                                                        }
                                                    }
if (checksum_bytes > 16) {
                                                        {
#line 147 "zsync/control.ae"
                                                            Control* _builder_ret = control_fail(c, "nonsensical hash lengths");
                                                            /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                                            /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                                            /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                                            return _builder_ret;
                                                        }
                                                    }
if (seq_matches < 1) {
                                                        {
#line 150 "zsync/control.ae"
                                                            Control* _builder_ret = control_fail(c, "nonsensical hash lengths");
                                                            /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                                            /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                                            /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                                            return _builder_ret;
                                                        }
                                                    }
if (seq_matches > 2) {
                                                        {
#line 153 "zsync/control.ae"
                                                            Control* _builder_ret = control_fail(c, "nonsensical hash lengths");
                                                            /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                                            /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                                            /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                                            return _builder_ret;
                                                        }
                                                    }
                                                }
                                            } else {
if (string_equals(key, "SHA-1") == 1) {
                                                    {
if (string_length(value) != 40) {
                                                            {
#line 157 "zsync/control.ae"
                                                                Control* _builder_ret = control_fail(c, "SHA-1 digest wrong length");
                                                                /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                                                /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                                                /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                                                return _builder_ret;
                                                            }
                                                        }
#line 159 "zsync/control.ae"
(c->checksum = value);
#line 160 "zsync/control.ae"
(c->checksum_method = "SHA-1");
                                                    }
                                                } else {
if (string_equals(key, "MTime") == 1) {
                                                        {
#line 162 "zsync/control.ae"
(c->mtime = value);
                                                        }
                                                    } else {
if (string_equals(key, "Safe") == 1) {
                                                            {
                                                            }
                                                        } else {
                                                            {
#line 166 "zsync/control.ae"
                                                                Control* _builder_ret = control_fail(c, "unknown header");
                                                                /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                                                                /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                                                                /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                                                                return _builder_ret;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
#line 168 "zsync/control.ae"
pos = (nl + 1);
                }
            }
        }
    }
if (c->filelen != 0) {
        {
if (c->blocksize != 0) {
                {
#line 174 "zsync/control.ae"
(c->blocks = (((c->filelen + c->blocksize) - 1) / c->blocksize));
                }
            }
        }
    }
if (c->filelen == 0) {
        {
#line 178 "zsync/control.ae"
            Control* _builder_ret = control_fail(c, "not a zsync file");
            /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
            /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
            /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
            return _builder_ret;
        }
    }
if (c->blocksize == 0) {
        {
#line 181 "zsync/control.ae"
            Control* _builder_ret = control_fail(c, "not a zsync file");
            /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
            /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
            /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
            return _builder_ret;
        }
    }
#line 184 "zsync/control.ae"
RcksumState* rs = rcksum_new(c->blocks, c->blocksize, rsum_bytes, checksum_bytes, seq_matches);
#line 185 "zsync/control.ae"
(c->rs = rcksum_state_as_ptr(rs));
#line 188 "zsync/control.ae"
int bp = header_end;
#line 189 "zsync/control.ae"
int i = 0;
    int rsum_val;
    int a;
    int b;
while (i < c->blocks) {
        {
if (((bp + rsum_bytes) + checksum_bytes) > data_len) {
                {
#line 192 "zsync/control.ae"
                    Control* _builder_ret = control_fail(c, "control file truncated in checksum table");
                    /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
                    /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
                    /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
                    return _builder_ret;
                }
            }
#line 197 "zsync/control.ae"
rsum_val = control_be_decode(data, data_len, bp, rsum_bytes);
#line 198 "zsync/control.ae"
a = ((rsum_val >> 16) & 0xffff);
#line 199 "zsync/control.ae"
b = (rsum_val & 0xffff);
#line 200 "zsync/control.ae"
bp = (bp + rsum_bytes);
#line 203 "zsync/control.ae"
{ const char* _tmp_old = md; md = control_pad_md4(data, data_len, bp, checksum_bytes); if (_heap_md) aether_heap_str_free(_tmp_old); _heap_md = 0; }
#line 204 "zsync/control.ae"
bp = (bp + checksum_bytes);
#line 206 "zsync/control.ae"
rcksum_add_target_block(rs, i, a, b, md);
#line 207 "zsync/control.ae"
i = (i + 1);
        }
    }
#line 210 "zsync/control.ae"
(c->ok = 1);
#line 211 "zsync/control.ae"
    Control* _builder_ret = c;
    /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
    /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
    /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
    /* deferred */ if (_heap_key) { aether_heap_str_free(key); key = NULL; _heap_key = 0; }
    /* deferred */ if (_heap_line) { aether_heap_str_free(line); line = NULL; _heap_line = 0; }
}

#line 217 "zsync/control.ae"
static const char* control_pad_md4(const char* data, int data_len, int off, int n) {
#line 218 "zsync/control.ae"
void* b = fileio_buf_alloc(16);
#line 219 "zsync/control.ae"
int i = 0;
while (i < 16) {
        {
if (i < n) {
                {
#line 222 "zsync/control.ae"
fileio_buf_set(b, i, (string_char_at_n(data, data_len, (off + i)) & 0xff));
                }
            }
#line 224 "zsync/control.ae"
i = (i + 1);
        }
    }
#line 230 "zsync/control.ae"
    return fileio_dup16(b);
}

#line 235 "zsync/control.ae"
static const char* control_substr(const char* s, int start, int fin) {
#line 236 "zsync/control.ae"
    return aether_uniform_heap_str((const char*)(string_substring(s, start, fin)), 1);
}

#line 240 "zsync/control.ae"
static int control_index_byte(const char* data, int data_len, int from, int target) {
#line 241 "zsync/control.ae"
int i = from;
while (i < data_len) {
        {
if ((string_char_at_n(data, data_len, i) & 0xff) == target) {
                {
#line 244 "zsync/control.ae"
                    return i;
                }
            }
#line 246 "zsync/control.ae"
i = (i + 1);
        }
    }
#line 248 "zsync/control.ae"
    return -1;
}

#line 252 "zsync/control.ae"
static int control_find_sep(const char* line) {
#line 253 "zsync/control.ae"
int n = string_length(line);
#line 254 "zsync/control.ae"
int i = 0;
while (i < (n - 1)) {
        {
if ((string_char_at(line, i) & 0xff) == 58) {
                {
if ((string_char_at(line, (i + 1)) & 0xff) == 32) {
                        {
#line 258 "zsync/control.ae"
                            return i;
                        }
                    }
                }
            }
#line 261 "zsync/control.ae"
i = (i + 1);
        }
    }
#line 263 "zsync/control.ae"
    return -1;
}

#line 266 "zsync/control.ae"
static int control_parse_int(const char* s) {
#line 267 "zsync/control.ae"
int ok = string_try_long(s);
if (ok == 0) {
        {
#line 269 "zsync/control.ae"
            return 0;
        }
    }
#line 271 "zsync/control.ae"
    return string_get_long(s);
}

#line 275 "zsync/control.ae"
static _tuple_int_int_int_int control_parse_hash_lengths(const char* value) {
#line 276 "zsync/control.ae"
void* parts = string_split(value, ",");
if (string_array_size(parts) != 3) {
        {
#line 278 "zsync/control.ae"
            return (_tuple_int_int_int_int){0, 0, 0, 0};
        }
    }
#line 280 "zsync/control.ae"
void* p0 = string_array_get(parts, 0);
#line 281 "zsync/control.ae"
void* p1 = string_array_get(parts, 1);
#line 282 "zsync/control.ae"
void* p2 = string_array_get(parts, 2);
#line 283 "zsync/control.ae"
int sm = control_parse_int_p(p0);
#line 284 "zsync/control.ae"
int rb = control_parse_int_p(p1);
#line 285 "zsync/control.ae"
int cb = control_parse_int_p(p2);
#line 286 "zsync/control.ae"
    return (_tuple_int_int_int_int){sm, rb, cb, 1};
}

#line 291 "zsync/control.ae"
static int control_parse_int_p(void* p) {
    int _heap_s = 0; (void)_heap_s;
    const char* s = NULL;
#line 292 "zsync/control.ae"
{ const char* _tmp_old = s; s = string_concat(fileio_buf_as_string(p), ""); if (_heap_s) aether_heap_str_free(_tmp_old); _heap_s = 1; }
#line 293 "zsync/control.ae"
    int _builder_ret = control_parse_int(s);
    /* deferred */ if (_heap_s) { aether_heap_str_free(s); s = NULL; _heap_s = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_s) { aether_heap_str_free(s); s = NULL; _heap_s = 0; }
}

#line 33 "zsync/download.ae"
static int download_br_start(ByteRange* b) {
    return b->start;
}

#line 34 "zsync/download.ae"
static int download_br_fin(ByteRange* b) {
    return b->fin;
}

#line 37 "zsync/download.ae"
static int download_br_count(void* l) {
#line 38 "zsync/download.ae"
    return list_size(l);
}

#line 41 "zsync/download.ae"
static int download_br_at_start(void* l, int i) {
#line 42 "zsync/download.ae"
ByteRange* b = ((ByteRange*)(list_get_raw(l, i)));
#line 43 "zsync/download.ae"
    return b->start;
}

#line 46 "zsync/download.ae"
static int download_br_at_fin(void* l, int i) {
#line 47 "zsync/download.ae"
ByteRange* b = ((ByteRange*)(list_get_raw(l, i)));
#line 48 "zsync/download.ae"
    return b->fin;
}

#line 52 "zsync/download.ae"
static State* download_new_state(void* ctl, int fd) {
#line 53 "zsync/download.ae"
void* raw = malloc(48);
#line 54 "zsync/download.ae"
State* s = ((State*)(raw));
#line 55 "zsync/download.ae"
(s->ctl = ctl);
#line 56 "zsync/download.ae"
(s->rs = control_ctl_rs(ctl));
#line 57 "zsync/download.ae"
(s->filelen = control_ctl_filelen(ctl));
#line 58 "zsync/download.ae"
(s->blocks = control_ctl_blocks(ctl));
#line 59 "zsync/download.ae"
(s->blocksize = control_ctl_blocksize(ctl));
#line 60 "zsync/download.ae"
(s->fd = fd);
#line 61 "zsync/download.ae"
rcksum_set_target_fd_p(s->rs, fd);
#line 62 "zsync/download.ae"
    return s;
}

#line 66 "zsync/download.ae"
static int download_status(State* s) {
#line 67 "zsync/download.ae"
int todo = rcksum_blocks_todo_p(s->rs);
if (todo == s->blocks) {
        {
#line 69 "zsync/download.ae"
            return 0;
        }
    }
if (todo > 0) {
        {
#line 72 "zsync/download.ae"
            return 1;
        }
    }
#line 74 "zsync/download.ae"
    return 2;
}

#line 78 "zsync/download.ae"
static _tuple_int_int download_progress(State* s) {
#line 79 "zsync/download.ae"
int todo = rcksum_blocks_todo_p(s->rs);
#line 80 "zsync/download.ae"
int got = ((s->blocks - todo) * s->blocksize);
#line 81 "zsync/download.ae"
int total = (s->blocks * s->blocksize);
#line 82 "zsync/download.ae"
    return (_tuple_int_int){got, total};
}

#line 86 "zsync/download.ae"
static void download_submit_source(State* s, const char* data, int data_len) {
#line 87 "zsync/download.ae"
rcksum_submit_source_buffer_p(s->rs, data, data_len);
}

#line 92 "zsync/download.ae"
static void* download_needed_byte_ranges(State* s) {
#line 93 "zsync/download.ae"
void* block_ranges = rcksum_needed_block_ranges_p(s->rs);
#line 94 "zsync/download.ae"
void* out = list_new();
#line 95 "zsync/download.ae"
int n = rcksum_ranges_list_len(block_ranges);
#line 96 "zsync/download.ae"
int i = 0;
    int bstart;
    int bend;
    int rstart;
    int rend;
    ByteRange* br;
while (i < n) {
        {
#line 98 "zsync/download.ae"
bstart = rcksum_ranges_list_start(block_ranges, i);
#line 99 "zsync/download.ae"
bend = rcksum_ranges_list_fin(block_ranges, i);
#line 100 "zsync/download.ae"
rstart = (bstart * s->blocksize);
#line 101 "zsync/download.ae"
rend = (((bend + 1) * s->blocksize) - 1);
if (rend >= s->filelen) {
                {
#line 103 "zsync/download.ae"
rend = (s->filelen - 1);
                }
            }
#line 105 "zsync/download.ae"
br = ((ByteRange*)(malloc(16)));
#line 106 "zsync/download.ae"
(br->start = rstart);
#line 107 "zsync/download.ae"
(br->fin = rend);
#line 108 "zsync/download.ae"
list_add_raw(out, br);
#line 109 "zsync/download.ae"
i = (i + 1);
        }
    }
#line 111 "zsync/download.ae"
    return out;
}

#line 117 "zsync/download.ae"
static int download_submit_target_data(State* s, int offset, const char* data, int data_len) {
    int _heap_block = 0; (void)_heap_block;
    const char* block = NULL;
    int _heap_padded = 0; (void)_heap_padded;
    const char* padded = NULL;
#line 118 "zsync/download.ae"
int received = 0;
#line 119 "zsync/download.ae"
int id = (offset / s->blocksize);
#line 120 "zsync/download.ae"
int pos = 0;
while ((pos + s->blocksize) <= data_len) {
        {
#line 122 "zsync/download.ae"
{ const char* _tmp_old = block; block = fileio_slice(data, data_len, pos, s->blocksize); if (_heap_block) aether_heap_str_free(_tmp_old); _heap_block = 1; }
#line 123 "zsync/download.ae"
rcksum_submit_blocks_p(s->rs, block, s->blocksize, id, id);
#line 124 "zsync/download.ae"
received = (received + s->blocksize);
#line 125 "zsync/download.ae"
pos = (pos + s->blocksize);
#line 126 "zsync/download.ae"
id = (id + 1);
        }
    }
if (pos < data_len) {
        {
#line 131 "zsync/download.ae"
int rem = (data_len - pos);
if (id == (s->blocks - 1)) {
                {
#line 133 "zsync/download.ae"
{ const char* _tmp_old = padded; padded = fileio_pad_block(data, data_len, pos, rem, s->blocksize); if (_heap_padded) aether_heap_str_free(_tmp_old); _heap_padded = 1; }
#line 134 "zsync/download.ae"
rcksum_submit_blocks_p(s->rs, padded, s->blocksize, id, id);
#line 135 "zsync/download.ae"
received = (received + rem);
                }
            }
        }
    }
#line 138 "zsync/download.ae"
    int _builder_ret = received;
    /* deferred */ if (_heap_padded) { aether_heap_str_free(padded); padded = NULL; _heap_padded = 0; }
    /* deferred */ if (_heap_block) { aether_heap_str_free(block); block = NULL; _heap_block = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_padded) { aether_heap_str_free(padded); padded = NULL; _heap_padded = 0; }
    /* deferred */ if (_heap_block) { aether_heap_str_free(block); block = NULL; _heap_block = 0; }
}

#line 144 "zsync/download.ae"
static const char* download_complete(State* s) {
    int _heap_want = 0; (void)_heap_want;
    const char* want = NULL;
    int _heap_out = 0; (void)_heap_out;
    const char* out = NULL;
    int _heap_got = 0; (void)_heap_got;
    const char* got = NULL;
    int _heap_e = 0; (void)_heap_e;
    const char* e = NULL;
if (rcksum_blocks_todo_p(s->rs) > 0) {
        {
#line 146 "zsync/download.ae"
            const char* _builder_ret = "file is not complete";
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_got) { aether_heap_str_free(got); got = NULL; _heap_got = 0; }
            /* deferred */ if (_heap_out) { aether_heap_str_free(out); out = NULL; _heap_out = 0; }
            /* deferred */ if (_heap_want) { aether_heap_str_free(want); want = NULL; _heap_want = 0; }
            return _builder_ret;
        }
    }
#line 148 "zsync/download.ae"
fileio_truncate_to(s->fd, s->filelen);
#line 149 "zsync/download.ae"
fileio_sync_fd(s->fd);
#line 151 "zsync/download.ae"
{ const char* _tmp_old = want; want = control_ctl_checksum(s->ctl); if (_heap_want) aether_heap_str_free(_tmp_old); _heap_want = 0; }
if (strcmp(_aether_safe_str(want), _aether_safe_str("")) == 0) {
        {
#line 153 "zsync/download.ae"
            const char* _builder_ret = "";
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_got) { aether_heap_str_free(got); got = NULL; _heap_got = 0; }
            /* deferred */ if (_heap_out) { aether_heap_str_free(out); out = NULL; _heap_out = 0; }
            /* deferred */ if (_heap_want) { aether_heap_str_free(want); want = NULL; _heap_want = 0; }
            return _builder_ret;
        }
    }
#line 155 "zsync/download.ae"
    _tuple_string_int _tup2 = fileio_read_at(s->fd, s->filelen, 0);
    { const char* _tmp_old = out; out = _tup2._0; if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 1; }
    int n = _tup2._1;
if (n != s->filelen) {
        {
#line 157 "zsync/download.ae"
            const char* _builder_ret = "failed to read back file for verification";
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_got) { aether_heap_str_free(got); got = NULL; _heap_got = 0; }
            /* deferred */ if (_heap_out) { aether_heap_str_free(out); out = NULL; _heap_out = 0; }
            /* deferred */ if (_heap_want) { aether_heap_str_free(want); want = NULL; _heap_want = 0; }
            return _builder_ret;
        }
    }
#line 159 "zsync/download.ae"
    _tuple_string_string _tup3 = cryptography_sha1_hex(out, n);
    { const char* _tmp_old = got; got = _tup3._0; if (_heap_got) aether_heap_str_free(_tmp_old); _heap_got = 1; }
    { const char* _tmp_old = e; e = _tup3._1; if (_heap_e) aether_heap_str_free(_tmp_old); _heap_e = 0; }
if (strcmp(_aether_safe_str(e), _aether_safe_str("")) != 0) {
        {
#line 161 "zsync/download.ae"
            const char* _builder_ret = "sha1 failed";
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_got) { aether_heap_str_free(got); got = NULL; _heap_got = 0; }
            /* deferred */ if (_heap_out) { aether_heap_str_free(out); out = NULL; _heap_out = 0; }
            /* deferred */ if (_heap_want) { aether_heap_str_free(want); want = NULL; _heap_want = 0; }
            return _builder_ret;
        }
    }
if (({ const char* _ad_0 = (const char*)(download_lower(got)); const char* _ad_1 = (const char*)(download_lower(want)); int _ad_r = string_equals(_ad_0, _ad_1); aether_heap_str_free(_ad_0); aether_heap_str_free(_ad_1); _ad_r; }) == 1) {
        {
#line 164 "zsync/download.ae"
            const char* _builder_ret = "";
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_got) { aether_heap_str_free(got); got = NULL; _heap_got = 0; }
            /* deferred */ if (_heap_out) { aether_heap_str_free(out); out = NULL; _heap_out = 0; }
            /* deferred */ if (_heap_want) { aether_heap_str_free(want); want = NULL; _heap_want = 0; }
            return _builder_ret;
        }
    }
#line 166 "zsync/download.ae"
    const char* _builder_ret = "checksum mismatch";
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_got) { aether_heap_str_free(got); got = NULL; _heap_got = 0; }
    /* deferred */ if (_heap_out) { aether_heap_str_free(out); out = NULL; _heap_out = 0; }
    /* deferred */ if (_heap_want) { aether_heap_str_free(want); want = NULL; _heap_want = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_got) { aether_heap_str_free(got); got = NULL; _heap_got = 0; }
    /* deferred */ if (_heap_out) { aether_heap_str_free(out); out = NULL; _heap_out = 0; }
    /* deferred */ if (_heap_want) { aether_heap_str_free(want); want = NULL; _heap_want = 0; }
}

#line 169 "zsync/download.ae"
static const char* download_lower(const char* s) {
#line 170 "zsync/download.ae"
    return aether_uniform_heap_str((const char*)(string_to_lower(s)), 1);
}

#line 174 "zsync/download.ae"
static const char* download_stats_line(State* s) {
    int _heap_out = 0; (void)_heap_out;
    const char* out = NULL;
#line 175 "zsync/download.ae"
int hh = rcksum_stat_hash_hit_p(s->rs);
#line 176 "zsync/download.ae"
int wh = rcksum_stat_weak_hit_p(s->rs);
#line 177 "zsync/download.ae"
int sh = rcksum_stat_strong_hit_p(s->rs);
#line 178 "zsync/download.ae"
int ck = rcksum_stat_checksummed_p(s->rs);
#line 179 "zsync/download.ae"
{ const char* _tmp_old = out; out = ({ const char* _ad_2 = (const char*)(string_from_int(hh)); const char* _ad_r = string_concat("hash stats: hash hit ", _ad_2); aether_heap_str_free(_ad_2); _ad_r; }); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 1; }
#line 180 "zsync/download.ae"
{ const char* _tmp_old = out; out = string_concat(out, ", weak hit "); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 1; }
#line 181 "zsync/download.ae"
{ const char* _tmp_old = out; out = ({ const char* _ad_3 = (const char*)(string_from_int(wh)); const char* _ad_r = string_concat(out, _ad_3); aether_heap_str_free(_ad_3); _ad_r; }); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 1; }
#line 182 "zsync/download.ae"
{ const char* _tmp_old = out; out = string_concat(out, ", checksums calculated "); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 1; }
#line 183 "zsync/download.ae"
{ const char* _tmp_old = out; out = ({ const char* _ad_4 = (const char*)(string_from_int(ck)); const char* _ad_r = string_concat(out, _ad_4); aether_heap_str_free(_ad_4); _ad_r; }); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 1; }
#line 184 "zsync/download.ae"
{ const char* _tmp_old = out; out = string_concat(out, ", strong hit "); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 1; }
#line 185 "zsync/download.ae"
{ const char* _tmp_old = out; out = ({ const char* _ad_5 = (const char*)(string_from_int(sh)); const char* _ad_r = string_concat(out, _ad_5); aether_heap_str_free(_ad_5); _ad_r; }); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 1; }
#line 186 "zsync/download.ae"
    const char* _no_defer_ret = aether_uniform_heap_str((const char*)(out), _heap_out);
    return _no_defer_ret;
}

#line 18 "cmd/clientlib.ae"
static const char* clientlib_basic_auth(const char* creds) {
    int _heap_enc = 0; (void)_heap_enc;
    const char* enc = NULL;
    int _heap_e = 0; (void)_heap_e;
    const char* e = NULL;
if (string_equals(creds, "") == 1) {
        {
#line 20 "cmd/clientlib.ae"
            const char* _builder_ret = aether_uniform_heap_str((const char*)(""), 0);
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_enc) { aether_heap_str_free(enc); enc = NULL; _heap_enc = 0; }
            return _builder_ret;
        }
    }
#line 22 "cmd/clientlib.ae"
    _tuple_string_string _tup4 = cryptography_base64_encode_padded(creds, string_length(creds));
    { const char* _tmp_old = enc; enc = _tup4._0; if (_heap_enc) aether_heap_str_free(_tmp_old); _heap_enc = 0; }
    { const char* _tmp_old = e; e = _tup4._1; if (_heap_e) aether_heap_str_free(_tmp_old); _heap_e = 0; }
if (strcmp(_aether_safe_str(e), _aether_safe_str("")) != 0) {
        {
#line 24 "cmd/clientlib.ae"
            const char* _builder_ret = aether_uniform_heap_str((const char*)(""), 0);
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_enc) { aether_heap_str_free(enc); enc = NULL; _heap_enc = 0; }
            return _builder_ret;
        }
    }
#line 26 "cmd/clientlib.ae"
    const char* _builder_ret = aether_uniform_heap_str((const char*)(string_concat("Basic ", enc)), 1);
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_enc) { aether_heap_str_free(enc); enc = NULL; _heap_enc = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_enc) { aether_heap_str_free(enc); enc = NULL; _heap_enc = 0; }
}

#line 32 "cmd/clientlib.ae"
static _tuple_string_int_int_string clientlib_http_get_auth(const char* url, const char* auth, const char* ims) {
    int _heap_err = 0; (void)_heap_err;
    const char* err = NULL;
    int _heap_body = 0; (void)_heap_body;
    const char* body = NULL;
    int _heap_owned = 0; (void)_heap_owned;
    const char* owned = NULL;
#line 33 "cmd/clientlib.ae"
void* req = client_request("GET", url);
#line 34 "cmd/clientlib.ae"
client_set_header(req, "User-Agent", "zsync-aether/0.7.1");
if (string_equals(auth, "") == 0) {
        {
#line 36 "cmd/clientlib.ae"
client_set_header(req, "Authorization", auth);
        }
    }
if (string_equals(ims, "") == 0) {
        {
#line 39 "cmd/clientlib.ae"
client_set_header(req, "If-Modified-Since", ims);
        }
    }
#line 41 "cmd/clientlib.ae"
    _tuple_ptr_string _tup5 = client_send_request(req);
    void* resp = _tup5._0;
    { const char* _tmp_old = err; err = _tup5._1; if (_heap_err) aether_heap_str_free(_tmp_old); _heap_err = 1; }
#line 42 "cmd/clientlib.ae"
client_request_free(req);
if (strcmp(_aether_safe_str(err), _aether_safe_str("")) != 0) {
        {
#line 44 "cmd/clientlib.ae"
            _tuple_string_int_int_string _builder_ret = (_tuple_string_int_int_string){aether_uniform_heap_str((const char*)(""), 0), 0, 0, aether_uniform_heap_str((const char*)(err), _heap_err)};
            /* deferred */ if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
            return _builder_ret;
        }
    }
#line 46 "cmd/clientlib.ae"
int status = client_response_status(resp);
if (status == 304) {
        {
#line 48 "cmd/clientlib.ae"
client_response_free(resp);
#line 49 "cmd/clientlib.ae"
            _tuple_string_int_int_string _builder_ret = (_tuple_string_int_int_string){aether_uniform_heap_str((const char*)(""), 0), 0, 304, aether_uniform_heap_str((const char*)(""), 0)};
            /* deferred */ if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
            return _builder_ret;
        }
    }
if (status != 200) {
        {
#line 52 "cmd/clientlib.ae"
client_response_free(resp);
#line 53 "cmd/clientlib.ae"
            _tuple_string_int_int_string _builder_ret = (_tuple_string_int_int_string){aether_uniform_heap_str((const char*)(""), 0), 0, status, aether_uniform_heap_str((const char*)(({ const char* _ad_6 = (const char*)(clientlib_int_str(status)); const char* _ad_r = clientlib_concat3("http status ", _ad_6, ""); aether_heap_str_free(_ad_6); _ad_r; })), 1)};
            /* deferred */ if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
            return _builder_ret;
        }
    }
#line 55 "cmd/clientlib.ae"
int n = client_response_body_length(resp);
#line 56 "cmd/clientlib.ae"
{ const char* _tmp_old = body; body = client_response_body(resp); if (_heap_body) aether_heap_str_free(_tmp_old); _heap_body = 0; }
#line 57 "cmd/clientlib.ae"
{ const char* _tmp_old = owned; owned = clientlib_copy_bytes(body, n); if (_heap_owned) aether_heap_str_free(_tmp_old); _heap_owned = 1; }
#line 58 "cmd/clientlib.ae"
client_response_free(resp);
#line 59 "cmd/clientlib.ae"
    _tuple_string_int_int_string _builder_ret = (_tuple_string_int_int_string){aether_uniform_heap_str((const char*)(owned), _heap_owned), n, 200, aether_uniform_heap_str((const char*)(""), 0)};
    /* deferred */ if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
}

#line 70 "cmd/clientlib.ae"
static _tuple_string_int_string clientlib_http_get_range_auth(const char* url, int start, int fin, const char* auth) {
    int _heap_err = 0; (void)_heap_err;
    const char* err = NULL;
    int _heap_body = 0; (void)_heap_body;
    const char* body = NULL;
    int _heap_owned = 0; (void)_heap_owned;
    const char* owned = NULL;
#line 71 "cmd/clientlib.ae"
void* req = client_request("GET", url);
#line 72 "cmd/clientlib.ae"
client_set_header(req, "User-Agent", "zsync-aether/0.7.1");
#line 73 "cmd/clientlib.ae"
({ const char* _ad_7 = (const char*)(clientlib_range_header(start, fin)); const char* _ad_r = client_set_header(req, "Range", _ad_7); aether_heap_str_free(_ad_7); _ad_r; });
if (string_equals(auth, "") == 0) {
        {
#line 75 "cmd/clientlib.ae"
client_set_header(req, "Authorization", auth);
        }
    }
#line 77 "cmd/clientlib.ae"
    _tuple_ptr_string _tup6 = client_send_request(req);
    void* resp = _tup6._0;
    { const char* _tmp_old = err; err = _tup6._1; if (_heap_err) aether_heap_str_free(_tmp_old); _heap_err = 1; }
#line 78 "cmd/clientlib.ae"
client_request_free(req);
if (strcmp(_aether_safe_str(err), _aether_safe_str("")) != 0) {
        {
#line 80 "cmd/clientlib.ae"
            _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(""), 0), 0, aether_uniform_heap_str((const char*)(err), _heap_err)};
            /* deferred */ if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
            return _builder_ret;
        }
    }
#line 82 "cmd/clientlib.ae"
int status = client_response_status(resp);
if (status != 206) {
        {
#line 84 "cmd/clientlib.ae"
client_response_free(resp);
#line 85 "cmd/clientlib.ae"
            _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(""), 0), 0, aether_uniform_heap_str((const char*)(({ const char* _ad_8 = (const char*)(clientlib_int_str(status)); const char* _ad_r = clientlib_concat3("expected 206, got ", _ad_8, ""); aether_heap_str_free(_ad_8); _ad_r; })), 1)};
            /* deferred */ if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
            return _builder_ret;
        }
    }
#line 87 "cmd/clientlib.ae"
int n = client_response_body_length(resp);
#line 88 "cmd/clientlib.ae"
{ const char* _tmp_old = body; body = client_response_body(resp); if (_heap_body) aether_heap_str_free(_tmp_old); _heap_body = 0; }
#line 89 "cmd/clientlib.ae"
{ const char* _tmp_old = owned; owned = clientlib_copy_bytes(body, n); if (_heap_owned) aether_heap_str_free(_tmp_old); _heap_owned = 1; }
#line 90 "cmd/clientlib.ae"
client_response_free(resp);
#line 91 "cmd/clientlib.ae"
    _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(owned), _heap_owned), n, aether_uniform_heap_str((const char*)(""), 0)};
    /* deferred */ if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
}

#line 99 "cmd/clientlib.ae"
static const char* clientlib_range_header(int start, int fin) {
#line 100 "cmd/clientlib.ae"
    return aether_uniform_heap_str((const char*)(({ const char* _ad_9 = (const char*)(clientlib_int_str(start)); const char* _ad_10 = (const char*)(({ const char* _ad_11 = (const char*)(clientlib_int_str(fin)); const char* _ad_r = clientlib_concat3("-", _ad_11, ""); aether_heap_str_free(_ad_11); _ad_r; })); const char* _ad_r = clientlib_concat3("bytes=", _ad_9, _ad_10); aether_heap_str_free(_ad_9); aether_heap_str_free(_ad_10); _ad_r; })), 1);
}

#line 105 "cmd/clientlib.ae"
static const char* clientlib_copy_bytes(const char* body, int n) {
#line 106 "cmd/clientlib.ae"
void* b = fileio_buf_alloc(n);
#line 107 "cmd/clientlib.ae"
int i = 0;
while (i < n) {
        {
#line 109 "cmd/clientlib.ae"
fileio_buf_set(b, i, (string_char_at_n(body, n, i) & 0xff));
#line 110 "cmd/clientlib.ae"
i = (i + 1);
        }
    }
#line 112 "cmd/clientlib.ae"
    return aether_uniform_heap_str((const char*)(fileio_buf_to_str(b, n)), 1);
}

#line 127 "cmd/clientlib.ae"
static const char* clientlib_fetch_remaining(void* st, const char* url, const char* auth) {
    int _heap_chunk = 0; (void)_heap_chunk;
    const char* chunk = NULL;
    int _heap_e = 0; (void)_heap_e;
    const char* e = NULL;
    void* needed;
    int nn;
    int i;
    int rstart;
    int rfin;
while (download_status(st) < 2) {
        {
#line 129 "cmd/clientlib.ae"
needed = download_needed_byte_ranges(st);
#line 130 "cmd/clientlib.ae"
nn = download_br_count(needed);
if (nn == 0) {
                {
#line 132 "cmd/clientlib.ae"
                    const char* _builder_ret = aether_uniform_heap_str((const char*)(""), 0);
                    if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
                    /* deferred */ if (_heap_chunk) { aether_heap_str_free(chunk); chunk = NULL; _heap_chunk = 0; }
                    return _builder_ret;
                }
            }
#line 134 "cmd/clientlib.ae"
i = 0;
while (i < nn) {
                {
#line 136 "cmd/clientlib.ae"
rstart = download_br_at_start(needed, i);
#line 137 "cmd/clientlib.ae"
rfin = download_br_at_fin(needed, i);
#line 138 "cmd/clientlib.ae"
                    _tuple_string_int_string _tup7 = clientlib_http_get_range_auth(url, rstart, rfin, auth);
                    { const char* _tmp_old = chunk; chunk = _tup7._0; if (_heap_chunk) aether_heap_str_free(_tmp_old); _heap_chunk = 1; }
                    int clen = _tup7._1;
                    { const char* _tmp_old = e; e = _tup7._2; if (_heap_e) aether_heap_str_free(_tmp_old); _heap_e = 1; }
if (strcmp(_aether_safe_str(e), _aether_safe_str("")) != 0) {
                        {
#line 140 "cmd/clientlib.ae"
                            const char* _builder_ret = aether_uniform_heap_str((const char*)(e), _heap_e);
                            /* deferred */ if (_heap_chunk) { aether_heap_str_free(chunk); chunk = NULL; _heap_chunk = 0; }
                            return _builder_ret;
                        }
                    }
#line 142 "cmd/clientlib.ae"
download_submit_target_data(st, rstart, chunk, clen);
#line 143 "cmd/clientlib.ae"
i = (i + 1);
                }
            }
        }
    }
#line 146 "cmd/clientlib.ae"
    const char* _builder_ret = aether_uniform_heap_str((const char*)(""), 0);
    if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_chunk) { aether_heap_str_free(chunk); chunk = NULL; _heap_chunk = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_chunk) { aether_heap_str_free(chunk); chunk = NULL; _heap_chunk = 0; }
}

#line 149 "cmd/clientlib.ae"
static const char* clientlib_concat3(const char* a, const char* b, const char* c) {
#line 150 "cmd/clientlib.ae"
    return aether_uniform_heap_str((const char*)(({ const char* _ad_12 = (const char*)(string_concat(a, b)); const char* _ad_r = string_concat(_ad_12, c); aether_heap_str_free(_ad_12); _ad_r; })), 1);
}

#line 153 "cmd/clientlib.ae"
static const char* clientlib_int_str(int v) {
#line 154 "cmd/clientlib.ae"
    return aether_uniform_heap_str((const char*)(string_from_int(v)), 1);
}

#line 29 "rcksum/fileio.ae"
static int fileio_open_rw(const char* path) {
#line 30 "rcksum/fileio.ae"
    return zsync_io_open_rw_trunc(aether_string_data(path));
}

#line 42 "rcksum/fileio.ae"
static int fileio_write_at(int fd, const char* data, int len, int offset) {
#line 43 "rcksum/fileio.ae"
int64_t written = zsync_io_pwrite(fd, aether_string_data(data), len, offset);
#line 44 "rcksum/fileio.ae"
    return written;
}

#line 50 "rcksum/fileio.ae"
static _tuple_string_int fileio_read_at(int fd, int len, int offset) {
    int _heap_buf = 0; (void)_heap_buf;
    const char* buf = NULL;
    int _heap_s = 0; (void)_heap_s;
    const char* s = NULL;
#line 51 "rcksum/fileio.ae"
{ const char* _tmp_old = buf; buf = zsync_io_pread_alloc(fd, len, offset); if (_heap_buf) aether_heap_str_free(_tmp_old); _heap_buf = 0; }
#line 52 "rcksum/fileio.ae"
int64_t n = zsync_io_last_read_len();
if (n < 0) {
        {
#line 54 "rcksum/fileio.ae"
            _tuple_string_int _builder_ret = (_tuple_string_int){aether_uniform_heap_str((const char*)(""), 0), (-(1))};
            /* deferred */ if (_heap_buf) { aether_heap_str_free(buf); buf = NULL; _heap_buf = 0; }
            return _builder_ret;
        }
    }
#line 58 "rcksum/fileio.ae"
{ const char* _tmp_old = s; s = string_new_with_length(buf, n); if (_heap_s) aether_heap_str_free(_tmp_old); _heap_s = 1; }
#line 59 "rcksum/fileio.ae"
    _tuple_string_int _builder_ret = (_tuple_string_int){aether_uniform_heap_str((const char*)(s), _heap_s), n};
    /* deferred */ if (_heap_buf) { aether_heap_str_free(buf); buf = NULL; _heap_buf = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_buf) { aether_heap_str_free(buf); buf = NULL; _heap_buf = 0; }
}

#line 62 "rcksum/fileio.ae"
static int fileio_truncate_to(int fd, int length) {
#line 63 "rcksum/fileio.ae"
    return zsync_io_ftruncate(fd, length);
}

#line 66 "rcksum/fileio.ae"
static int fileio_close_fd(int fd) {
#line 67 "rcksum/fileio.ae"
    return zsync_io_close(fd);
}

#line 70 "rcksum/fileio.ae"
static int fileio_sync_fd(int fd) {
#line 71 "rcksum/fileio.ae"
    return zsync_io_fsync(fd);
}

#line 85 "rcksum/fileio.ae"
static void* fileio_buf_alloc(int n) {
#line 86 "rcksum/fileio.ae"
    return zsync_buf_alloc(n);
}

#line 91 "rcksum/fileio.ae"
static const char* fileio_buf_alloc_str(int n) {
#line 92 "rcksum/fileio.ae"
    return aether_uniform_heap_str((const char*)(string_new_with_length(zsync_buf_alloc_str(n), n)), 1);
}

#line 95 "rcksum/fileio.ae"
static int fileio_buf_get(void* b, int i) {
#line 96 "rcksum/fileio.ae"
    return zsync_buf_get(b, i);
}

#line 99 "rcksum/fileio.ae"
static void fileio_buf_set(void* b, int i, int v) {
#line 100 "rcksum/fileio.ae"
zsync_buf_set(b, i, v);
}

#line 103 "rcksum/fileio.ae"
static void fileio_buf_or(void* b, int i, int v) {
#line 104 "rcksum/fileio.ae"
zsync_buf_or(b, i, v);
}

#line 114 "rcksum/fileio.ae"
static const char* fileio_buf_to_str(void* b, int n) {
    int _heap_s = 0; (void)_heap_s;
    const char* s = NULL;
#line 115 "rcksum/fileio.ae"
s = string_new_with_length(fileio_buf_as_string(b), n);
#line 119 "rcksum/fileio.ae"
string_retain(s);
#line 120 "rcksum/fileio.ae"
    const char* _no_defer_ret = aether_uniform_heap_str((const char*)(s), _heap_s);
    return _no_defer_ret;
}

#line 125 "rcksum/fileio.ae"
static const char* fileio_zero_prefix(const char* src, int src_len, int prefix) {
#line 126 "rcksum/fileio.ae"
void* out = zsync_buf_alloc(src_len);
#line 127 "rcksum/fileio.ae"
int i = 0;
while (i < src_len) {
        {
if (i < prefix) {
                {
#line 130 "rcksum/fileio.ae"
zsync_buf_set(out, i, 0);
                }
            } else {
                {
#line 132 "rcksum/fileio.ae"
zsync_buf_set(out, i, (string_char_at_n(src, src_len, i) & 0xff));
                }
            }
#line 134 "rcksum/fileio.ae"
i = (i + 1);
        }
    }
#line 136 "rcksum/fileio.ae"
    return aether_uniform_heap_str((const char*)(string_new_with_length(fileio_buf_as_string(out), src_len)), 1);
}

#line 141 "rcksum/fileio.ae"
static const char* fileio_pad_block(const char* src, int src_len, int start, int rem, int blocksize) {
#line 142 "rcksum/fileio.ae"
void* out = zsync_buf_alloc(blocksize);
#line 143 "rcksum/fileio.ae"
int i = 0;
while (i < rem) {
        {
#line 145 "rcksum/fileio.ae"
zsync_buf_set(out, i, (string_char_at_n(src, src_len, (start + i)) & 0xff));
#line 146 "rcksum/fileio.ae"
i = (i + 1);
        }
    }
#line 148 "rcksum/fileio.ae"
    return aether_uniform_heap_str((const char*)(string_new_with_length(fileio_buf_as_string(out), blocksize)), 1);
}

#line 155 "rcksum/fileio.ae"
static const char* fileio_slice(const char* src, int src_len, int start, int len) {
#line 156 "rcksum/fileio.ae"
void* out = zsync_buf_alloc(len);
#line 157 "rcksum/fileio.ae"
int i = 0;
while (i < len) {
        {
#line 159 "rcksum/fileio.ae"
zsync_buf_set(out, i, (string_char_at_n(src, src_len, (start + i)) & 0xff));
#line 160 "rcksum/fileio.ae"
i = (i + 1);
        }
    }
#line 162 "rcksum/fileio.ae"
    return aether_uniform_heap_str((const char*)(string_new_with_length(fileio_buf_as_string(out), len)), 1);
}

#line 167 "rcksum/fileio.ae"
static const char* fileio_buf_as_string(void* b) {
#line 168 "rcksum/fileio.ae"
    return zsync_buf_identity(b);
}

#line 176 "rcksum/fileio.ae"
static const char* fileio_rfc1123z(int epoch) {
#line 177 "rcksum/fileio.ae"
    return zsync_rfc1123z(epoch);
}

#line 195 "rcksum/fileio.ae"
static const char* fileio_dup16(void* b) {
#line 196 "rcksum/fileio.ae"
    return zsync_dup16(b);
}

// Import: std.json
// Import: rcksum.rcksum
#line 31 "rcksum/rcksum.ae"
static int rcksum_NO_BLOCK(void) {
#line 32 "rcksum/rcksum.ae"
    return -1;
}

#line 35 "rcksum/rcksum.ae"
static int rcksum_BITHASH_BITS(void) {
#line 36 "rcksum/rcksum.ae"
    return 3;
}

#line 85 "rcksum/rcksum.ae"
static void* rcksum_box_int(int v) {
#line 86 "rcksum/rcksum.ae"
void* raw = malloc(8);
#line 87 "rcksum/rcksum.ae"
IntCell* p = ((IntCell*)(raw));
#line 88 "rcksum/rcksum.ae"
(p->v = v);
#line 89 "rcksum/rcksum.ae"
    return p;
}

#line 92 "rcksum/rcksum.ae"
static int rcksum_unbox_int(void* p) {
#line 93 "rcksum/rcksum.ae"
IntCell* c = ((IntCell*)(p));
#line 94 "rcksum/rcksum.ae"
    return c->v;
}

#line 98 "rcksum/rcksum.ae"
static HashEntry* rcksum_entry_at(RcksumState* z, int b) {
#line 99 "rcksum/rcksum.ae"
    return ((HashEntry*)(list_get_raw(z->block_hashes, b)));
}

#line 102 "rcksum/rcksum.ae"
static HashEntry* rcksum_new_entry(void) {
#line 103 "rcksum/rcksum.ae"
void* raw = malloc(48);
#line 104 "rcksum/rcksum.ae"
HashEntry* e = ((HashEntry*)(raw));
#line 105 "rcksum/rcksum.ae"
(e->next = rcksum_NO_BLOCK());
#line 106 "rcksum/rcksum.ae"
(e->rsum_a = 0);
#line 107 "rcksum/rcksum.ae"
(e->rsum_b = 0);
#line 108 "rcksum/rcksum.ae"
(e->md4 = "");
#line 109 "rcksum/rcksum.ae"
    return e;
}

#line 114 "rcksum/rcksum.ae"
static RcksumState* rcksum_new(int nblocks, int block_size, int rsum_bytes, int checksum_bytes, int seq_matches) {
#line 115 "rcksum/rcksum.ae"
void* raw = malloc(160);
#line 116 "rcksum/rcksum.ae"
RcksumState* z = ((RcksumState*)(raw));
#line 117 "rcksum/rcksum.ae"
(z->blocks = nblocks);
#line 118 "rcksum/rcksum.ae"
(z->block_size = block_size);
#line 119 "rcksum/rcksum.ae"
(z->checksum_bytes = checksum_bytes);
#line 120 "rcksum/rcksum.ae"
(z->seq_matches = seq_matches);
#line 121 "rcksum/rcksum.ae"
(z->context = (block_size * seq_matches));
#line 122 "rcksum/rcksum.ae"
(z->skip = 0);
#line 123 "rcksum/rcksum.ae"
(z->bit_hash = NULL);
#line 124 "rcksum/rcksum.ae"
(z->bit_hash_mask = 0);
#line 125 "rcksum/rcksum.ae"
(z->bit_hash_len = 0);
#line 126 "rcksum/rcksum.ae"
(z->rsum_hash = NULL);
#line 127 "rcksum/rcksum.ae"
(z->fd = -1);
if (rsum_bytes < 3) {
        {
#line 130 "rcksum/rcksum.ae"
(z->rsum_a_mask = 0);
        }
    } else {
if (rsum_bytes == 3) {
            {
#line 132 "rcksum/rcksum.ae"
(z->rsum_a_mask = 0xff);
            }
        } else {
            {
#line 134 "rcksum/rcksum.ae"
(z->rsum_a_mask = 0xffff);
            }
        }
    }
#line 136 "rcksum/rcksum.ae"
(z->rsum_bits = (rsum_bytes * 8));
#line 139 "rcksum/rcksum.ae"
(z->block_shift = checksums_log2(block_size));
#line 142 "rcksum/rcksum.ae"
(z->block_hashes = list_new());
#line 143 "rcksum/rcksum.ae"
int i = 0;
while (i < nblocks) {
        {
#line 145 "rcksum/rcksum.ae"
list_add_raw(z->block_hashes, rcksum_new_entry());
#line 146 "rcksum/rcksum.ae"
i = (i + 1);
        }
    }
#line 149 "rcksum/rcksum.ae"
(z->known = ranges_ranges_as_ptr(ranges_new_ranges()));
#line 151 "rcksum/rcksum.ae"
(z->r0 = checksums_rsum_as_ptr(checksums_new_rsum(0, 0)));
#line 152 "rcksum/rcksum.ae"
(z->r1 = checksums_rsum_as_ptr(checksums_new_rsum(0, 0)));
#line 154 "rcksum/rcksum.ae"
void* sraw = malloc(32);
#line 155 "rcksum/rcksum.ae"
Stats* st = ((Stats*)(sraw));
#line 156 "rcksum/rcksum.ae"
(st->hash_hit = 0);
#line 157 "rcksum/rcksum.ae"
(st->weak_hit = 0);
#line 158 "rcksum/rcksum.ae"
(st->strong_hit = 0);
#line 159 "rcksum/rcksum.ae"
(st->checksummed = 0);
#line 160 "rcksum/rcksum.ae"
(z->stats = st);
#line 162 "rcksum/rcksum.ae"
    return z;
}

#line 165 "rcksum/rcksum.ae"
static void rcksum_set_target_fd(RcksumState* z, int fd) {
#line 166 "rcksum/rcksum.ae"
(z->fd = fd);
}

#line 171 "rcksum/rcksum.ae"
static void* rcksum_state_as_ptr(RcksumState* z) {
#line 172 "rcksum/rcksum.ae"
    return z;
}

#line 178 "rcksum/rcksum.ae"
static void rcksum_set_target_fd_p(void* z, int fd) {
#line 179 "rcksum/rcksum.ae"
rcksum_set_target_fd(((RcksumState*)(z)), fd);
}

#line 182 "rcksum/rcksum.ae"
static int rcksum_blocks_todo_p(void* z) {
#line 183 "rcksum/rcksum.ae"
    return rcksum_blocks_todo(((RcksumState*)(z)));
}

#line 186 "rcksum/rcksum.ae"
static int rcksum_submit_source_buffer_p(void* z, const char* data, int data_len) {
#line 187 "rcksum/rcksum.ae"
    return rcksum_submit_source_buffer(((RcksumState*)(z)), data, data_len);
}

#line 190 "rcksum/rcksum.ae"
static void rcksum_submit_blocks_p(void* z, const char* data, int data_len, int bfrom, int bto) {
#line 191 "rcksum/rcksum.ae"
rcksum_submit_blocks(((RcksumState*)(z)), data, data_len, bfrom, bto);
}

#line 194 "rcksum/rcksum.ae"
static void* rcksum_needed_block_ranges_p(void* z) {
#line 195 "rcksum/rcksum.ae"
    return rcksum_needed_block_ranges(((RcksumState*)(z)));
}

#line 200 "rcksum/rcksum.ae"
static int rcksum_ranges_list_len(void* l) {
#line 201 "rcksum/rcksum.ae"
    return ranges_list_len(l);
}

#line 204 "rcksum/rcksum.ae"
static int rcksum_ranges_list_start(void* l, int i) {
#line 205 "rcksum/rcksum.ae"
    return ranges_pair_start(ranges_list_pair(l, i));
}

#line 208 "rcksum/rcksum.ae"
static int rcksum_ranges_list_fin(void* l, int i) {
#line 209 "rcksum/rcksum.ae"
    return ranges_pair_fin(ranges_list_pair(l, i));
}

#line 212 "rcksum/rcksum.ae"
static _tuple_string_int rcksum_read_known_data_p(void* z, int len, int offset) {
#line 213 "rcksum/rcksum.ae"
    return rcksum_read_known_data(((RcksumState*)(z)), len, offset);
}

#line 216 "rcksum/rcksum.ae"
static void rcksum_add_target_block_p(void* z, int b, int rsum_a, int rsum_b, const char* md4) {
#line 217 "rcksum/rcksum.ae"
rcksum_add_target_block(((RcksumState*)(z)), b, rsum_a, rsum_b, md4);
}

#line 221 "rcksum/rcksum.ae"
static int rcksum_stat_hash_hit_p(void* z) {
#line 222 "rcksum/rcksum.ae"
RcksumState* s = ((RcksumState*)(z));
#line 223 "rcksum/rcksum.ae"
    return s->stats->hash_hit;
}

#line 225 "rcksum/rcksum.ae"
static int rcksum_stat_weak_hit_p(void* z) {
#line 226 "rcksum/rcksum.ae"
RcksumState* s = ((RcksumState*)(z));
#line 227 "rcksum/rcksum.ae"
    return s->stats->weak_hit;
}

#line 229 "rcksum/rcksum.ae"
static int rcksum_stat_strong_hit_p(void* z) {
#line 230 "rcksum/rcksum.ae"
RcksumState* s = ((RcksumState*)(z));
#line 231 "rcksum/rcksum.ae"
    return s->stats->strong_hit;
}

#line 233 "rcksum/rcksum.ae"
static int rcksum_stat_checksummed_p(void* z) {
#line 234 "rcksum/rcksum.ae"
RcksumState* s = ((RcksumState*)(z));
#line 235 "rcksum/rcksum.ae"
    return s->stats->checksummed;
}

#line 239 "rcksum/rcksum.ae"
static void rcksum_add_target_block(RcksumState* z, int b, int rsum_a, int rsum_b, const char* md4) {
if (b < z->blocks) {
        {
#line 241 "rcksum/rcksum.ae"
HashEntry* e = rcksum_entry_at(z, b);
#line 242 "rcksum/rcksum.ae"
(e->md4 = md4);
#line 243 "rcksum/rcksum.ae"
(e->rsum_a = (rsum_a & z->rsum_a_mask));
#line 244 "rcksum/rcksum.ae"
(e->rsum_b = (rsum_b & 0xffff));
#line 246 "rcksum/rcksum.ae"
(z->rsum_hash = NULL);
#line 247 "rcksum/rcksum.ae"
(z->bit_hash = NULL);
        }
    }
}

#line 251 "rcksum/rcksum.ae"
static int rcksum_blocks_todo(RcksumState* z) {
#line 252 "rcksum/rcksum.ae"
    return (z->blocks - ranges_got_blocks(z->known));
}

#line 256 "rcksum/rcksum.ae"
static int rcksum_calc_rhash(RcksumState* z, int b) {
#line 257 "rcksum/rcksum.ae"
HashEntry* e1 = rcksum_entry_at(z, b);
#line 258 "rcksum/rcksum.ae"
RSum* rs1 = checksums_new_rsum(e1->rsum_a, e1->rsum_b);
if (z->seq_matches > 1) {
        {
#line 260 "rcksum/rcksum.ae"
HashEntry* e2 = rcksum_entry_at(z, (b + 1));
#line 261 "rcksum/rcksum.ae"
RSum* rs2 = checksums_new_rsum(e2->rsum_a, e2->rsum_b);
#line 262 "rcksum/rcksum.ae"
            return checksums_calc_rhash_from_rsums(rs1, rs2, z->seq_matches, z->rsum_a_mask);
        }
    }
#line 264 "rcksum/rcksum.ae"
    return checksums_calc_rhash_from_rsums(rs1, rs1, z->seq_matches, z->rsum_a_mask);
}

#line 268 "rcksum/rcksum.ae"
static const char* rcksum_hkey(int h) {
#line 269 "rcksum/rcksum.ae"
    return aether_uniform_heap_str((const char*)(string_from_int((h & 0xffffffff))), 1);
}

#line 272 "rcksum/rcksum.ae"
static int rcksum_rsum_hash_get(RcksumState* z, int h) {
#line 273 "rcksum/rcksum.ae"
void* v = ({ const char* _ad_13 = (const char*)(rcksum_hkey(h)); void* _ad_r = map_get_raw(z->rsum_hash, aether_string_data(_ad_13)); aether_heap_str_free(_ad_13); _ad_r; });
if (v == NULL) {
        {
#line 275 "rcksum/rcksum.ae"
            return rcksum_NO_BLOCK();
        }
    }
#line 277 "rcksum/rcksum.ae"
    return (rcksum_unbox_int(v) - 1);
}

#line 280 "rcksum/rcksum.ae"
static void rcksum_rsum_hash_put(RcksumState* z, int h, int id) {
#line 281 "rcksum/rcksum.ae"
({ const char* _ad_14 = (const char*)(rcksum_hkey(h)); int _ad_r = map_put_raw(z->rsum_hash, aether_string_data(_ad_14), rcksum_box_int((id + 1))); aether_heap_str_free(_ad_14); _ad_r; });
}

#line 284 "rcksum/rcksum.ae"
static void rcksum_rsum_hash_del(RcksumState* z, int h) {
#line 285 "rcksum/rcksum.ae"
map_remove(z->rsum_hash, aether_string_data(rcksum_hkey(h)));
}

#line 289 "rcksum/rcksum.ae"
static void rcksum_build_hash(RcksumState* z) {
#line 290 "rcksum/rcksum.ae"
(z->rsum_hash = map_new());
#line 292 "rcksum/rcksum.ae"
int bit_hash_bits = (checksums_log2(z->blocks) + rcksum_BITHASH_BITS());
#line 293 "rcksum/rcksum.ae"
(z->bit_hash_mask = ((1 << bit_hash_bits) - 1));
#line 294 "rcksum/rcksum.ae"
(z->bit_hash_len = (((z->bit_hash_mask + 1) + 7) >> 3));
#line 295 "rcksum/rcksum.ae"
(z->bit_hash = fileio_buf_alloc(z->bit_hash_len));
#line 298 "rcksum/rcksum.ae"
int id = (z->blocks - z->seq_matches);
    int h;
    int nxt;
    HashEntry* e;
    int bit_idx;
    int bit_pos;
while (id >= 0) {
        {
#line 300 "rcksum/rcksum.ae"
h = rcksum_calc_rhash(z, id);
#line 301 "rcksum/rcksum.ae"
nxt = rcksum_rsum_hash_get(z, h);
#line 302 "rcksum/rcksum.ae"
e = rcksum_entry_at(z, id);
#line 303 "rcksum/rcksum.ae"
(e->next = nxt);
#line 304 "rcksum/rcksum.ae"
rcksum_rsum_hash_put(z, h, id);
#line 306 "rcksum/rcksum.ae"
bit_idx = ((h & z->bit_hash_mask) >> 3);
#line 307 "rcksum/rcksum.ae"
bit_pos = (h & 7);
if (bit_idx < z->bit_hash_len) {
                {
#line 309 "rcksum/rcksum.ae"
fileio_buf_or(z->bit_hash, bit_idx, (1 << bit_pos));
                }
            }
#line 311 "rcksum/rcksum.ae"
id = (id - 1);
        }
    }
}

#line 316 "rcksum/rcksum.ae"
static void rcksum_remove_block_from_hash(RcksumState* z, int id) {
if (z->rsum_hash == NULL) {
        {
#line 318 "rcksum/rcksum.ae"
            return;
        }
    }
if (id >= (z->blocks - (z->seq_matches - 1))) {
        {
#line 321 "rcksum/rcksum.ae"
            return;
        }
    }
#line 323 "rcksum/rcksum.ae"
int h = rcksum_calc_rhash(z, id);
#line 324 "rcksum/rcksum.ae"
int p = rcksum_rsum_hash_get(z, h);
if (p == id) {
        {
#line 326 "rcksum/rcksum.ae"
HashEntry* e = rcksum_entry_at(z, id);
#line 327 "rcksum/rcksum.ae"
int nxt = e->next;
if (nxt != rcksum_NO_BLOCK()) {
                {
#line 329 "rcksum/rcksum.ae"
rcksum_rsum_hash_put(z, h, nxt);
                }
            } else {
                {
#line 331 "rcksum/rcksum.ae"
rcksum_rsum_hash_del(z, h);
                }
            }
        }
    } else {
if (p != rcksum_NO_BLOCK()) {
            {
                HashEntry* pe;
while (p != rcksum_NO_BLOCK()) {
                    {
#line 335 "rcksum/rcksum.ae"
pe = rcksum_entry_at(z, p);
if (pe->next == id) {
                            {
#line 337 "rcksum/rcksum.ae"
(pe->next = rcksum_entry_at(z, id)->next);
#line 338 "rcksum/rcksum.ae"
p = rcksum_NO_BLOCK();
                            }
                        } else {
                            {
#line 340 "rcksum/rcksum.ae"
p = pe->next;
                            }
                        }
                    }
                }
            }
        }
    }
#line 344 "rcksum/rcksum.ae"
(rcksum_entry_at(z, id)->next = rcksum_NO_BLOCK());
}

#line 348 "rcksum/rcksum.ae"
static void* rcksum_needed_block_ranges(RcksumState* z) {
#line 349 "rcksum/rcksum.ae"
    return ranges_missing_blocks_between(z->known, 0, (z->blocks - 1));
}

#line 358 "rcksum/rcksum.ae"
static int rcksum_prefix_eq(const char* a, int a_len, const char* b, int b_len, int n) {
#line 359 "rcksum/rcksum.ae"
int i = 0;
    int ca;
    int cb;
while (i < n) {
        {
#line 361 "rcksum/rcksum.ae"
ca = (string_char_at_n(a, a_len, i) & 0xff);
#line 362 "rcksum/rcksum.ae"
cb = (string_char_at_n(b, b_len, i) & 0xff);
if (ca != cb) {
                {
#line 364 "rcksum/rcksum.ae"
                    return 0;
                }
            }
#line 366 "rcksum/rcksum.ae"
i = (i + 1);
        }
    }
#line 368 "rcksum/rcksum.ae"
    return 1;
}

#line 376 "rcksum/rcksum.ae"
static int rcksum_write_blocks(RcksumState* z, const char* data, int data_len, int bfrom, int bto, int next) {
#line 377 "rcksum/rcksum.ae"
int span = (((bto + 1) - bfrom) << z->block_shift);
#line 378 "rcksum/rcksum.ae"
int offset = (bfrom << z->block_shift);
#line 379 "rcksum/rcksum.ae"
fileio_write_at(z->fd, data, span, offset);
#line 381 "rcksum/rcksum.ae"
int id = bfrom;
while (id <= bto) {
        {
if (id == next) {
                {
#line 384 "rcksum/rcksum.ae"
next = rcksum_entry_at(z, id)->next;
                }
            }
#line 386 "rcksum/rcksum.ae"
ranges_add_to_ranges(z->known, id);
if (z->seq_matches == 2) {
                {
if (id != bto) {
                        {
#line 389 "rcksum/rcksum.ae"
rcksum_remove_block_from_hash(z, id);
                        }
                    } else {
if (ranges_contains(z->known, (bto + 1)) == 1) {
                            {
#line 391 "rcksum/rcksum.ae"
rcksum_remove_block_from_hash(z, id);
                            }
                        }
                    }
                }
            }
#line 394 "rcksum/rcksum.ae"
id = (id + 1);
        }
    }
if (z->seq_matches == 2) {
        {
if (bfrom > 0) {
                {
if (ranges_contains(z->known, (bfrom - 1)) == 1) {
                        {
#line 399 "rcksum/rcksum.ae"
rcksum_remove_block_from_hash(z, (bfrom - 1));
                        }
                    }
                }
            }
        }
    }
#line 403 "rcksum/rcksum.ae"
    return next;
}

#line 409 "rcksum/rcksum.ae"
static void rcksum_submit_blocks(RcksumState* z, const char* data, int data_len, int bfrom, int bto) {
    int _heap_block = 0; (void)_heap_block;
    const char* block = NULL;
    int _heap_md = 0; (void)_heap_md;
    const char* md = NULL;
    int _heap_stored = 0; (void)_heap_stored;
    const char* stored = NULL;
    int _heap_prefix = 0; (void)_heap_prefix;
    const char* prefix = NULL;
if (z->rsum_hash == NULL) {
        {
#line 411 "rcksum/rcksum.ae"
rcksum_build_hash(z);
        }
    }
#line 413 "rcksum/rcksum.ae"
int x = bfrom;
    int off;
while (x <= bto) {
        {
#line 415 "rcksum/rcksum.ae"
off = ((x - bfrom) << z->block_shift);
#line 416 "rcksum/rcksum.ae"
{ const char* _tmp_old = block; block = fileio_slice(data, data_len, off, z->block_size); if (_heap_block) aether_heap_str_free(_tmp_old); _heap_block = 1; }
#line 417 "rcksum/rcksum.ae"
{ const char* _tmp_old = md; md = checksums_calc_checksum(block, z->block_size); if (_heap_md) aether_heap_str_free(_tmp_old); _heap_md = 0; }
#line 418 "rcksum/rcksum.ae"
{ const char* _tmp_old = stored; stored = rcksum_entry_at(z, x)->md4; if (_heap_stored) aether_heap_str_free(_tmp_old); _heap_stored = 0; }
if (rcksum_prefix_eq(md, 16, stored, 16, z->checksum_bytes) == 0) {
                {
#line 420 "rcksum/rcksum.ae"
                    break;
                }
            }
#line 422 "rcksum/rcksum.ae"
x = (x + 1);
        }
    }
if (x > bfrom) {
        {
#line 427 "rcksum/rcksum.ae"
int span_len = ((x - bfrom) << z->block_shift);
#line 428 "rcksum/rcksum.ae"
{ const char* _tmp_old = prefix; prefix = fileio_slice(data, data_len, 0, span_len); if (_heap_prefix) aether_heap_str_free(_tmp_old); _heap_prefix = 1; }
#line 429 "rcksum/rcksum.ae"
rcksum_write_blocks(z, prefix, span_len, bfrom, (x - 1), rcksum_NO_BLOCK());
        }
    }
    /* deferred */ if (_heap_prefix) { aether_heap_str_free(prefix); prefix = NULL; _heap_prefix = 0; }
    /* deferred */ if (_heap_stored) { aether_heap_str_free(stored); stored = NULL; _heap_stored = 0; }
    /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
    /* deferred */ if (_heap_block) { aether_heap_str_free(block); block = NULL; _heap_block = 0; }
}

#line 436 "rcksum/rcksum.ae"
static int rcksum_match_block(RcksumState* z, const char* data, int data_len, int doff) {
#line 437 "rcksum/rcksum.ae"
int h = checksums_calc_rhash_from_rsums(z->r0, z->r1, z->seq_matches, z->rsum_a_mask);
#line 439 "rcksum/rcksum.ae"
int bit_idx = ((h & z->bit_hash_mask) >> 3);
#line 440 "rcksum/rcksum.ae"
int bit_pos = (h & 7);
if (z->bit_hash == NULL) {
        {
#line 442 "rcksum/rcksum.ae"
            return 0;
        }
    }
if (bit_idx >= z->bit_hash_len) {
        {
#line 445 "rcksum/rcksum.ae"
            return 0;
        }
    }
#line 447 "rcksum/rcksum.ae"
int bv = fileio_buf_get(z->bit_hash, bit_idx);
if ((bv & (1 << bit_pos)) == 0) {
        {
#line 449 "rcksum/rcksum.ae"
            return 0;
        }
    }
#line 451 "rcksum/rcksum.ae"
int e = rcksum_rsum_hash_get(z, h);
if (e == rcksum_NO_BLOCK()) {
        {
#line 453 "rcksum/rcksum.ae"
            return 0;
        }
    }
#line 455 "rcksum/rcksum.ae"
    return rcksum_check_chain(z, e, data, data_len, doff);
}

#line 461 "rcksum/rcksum.ae"
static int rcksum_check_chain(RcksumState* z, int id, const char* data, int data_len, int doff) {
    int _heap_md_cache0 = 0; (void)_heap_md_cache0;
    const char* md_cache0 = NULL;
    int _heap_md_cache1 = 0; (void)_heap_md_cache1;
    const char* md_cache1 = NULL;
    int _heap_block = 0; (void)_heap_block;
    const char* block = NULL;
    int _heap_md = 0; (void)_heap_md;
    const char* md = NULL;
    int _heap_cand = 0; (void)_heap_cand;
    const char* cand = NULL;
    int _heap_stored = 0; (void)_heap_stored;
    const char* stored = NULL;
    int _heap_prefix = 0; (void)_heap_prefix;
    const char* prefix = NULL;
#line 462 "rcksum/rcksum.ae"
int got = 0;
#line 464 "rcksum/rcksum.ae"
{ const char* _tmp_old = md_cache0; md_cache0 = ""; if (_heap_md_cache0) aether_heap_str_free(_tmp_old); _heap_md_cache0 = 0; }
#line 465 "rcksum/rcksum.ae"
{ const char* _tmp_old = md_cache1; md_cache1 = ""; if (_heap_md_cache1) aether_heap_str_free(_tmp_old); _heap_md_cache1 = 0; }
#line 466 "rcksum/rcksum.ae"
int cached = 0;
#line 468 "rcksum/rcksum.ae"
int next = id;
    HashEntry* e0;
    HashEntry* e1;
    int matching;
    int checkmd4;
    int boff;
    int next_known;
    int num_write;
    int span_len;
while (next != rcksum_NO_BLOCK()) {
        {
#line 470 "rcksum/rcksum.ae"
id = next;
#line 471 "rcksum/rcksum.ae"
next = rcksum_entry_at(z, id)->next;
#line 473 "rcksum/rcksum.ae"
(z->stats->hash_hit = (z->stats->hash_hit + 1));
#line 474 "rcksum/rcksum.ae"
e0 = rcksum_entry_at(z, id);
if (e0->rsum_a != (checksums_rsum_a(z->r0) & z->rsum_a_mask)) {
                {
#line 476 "rcksum/rcksum.ae"
                    continue;
                }
            }
if (e0->rsum_b != checksums_rsum_b(z->r0)) {
                {
#line 479 "rcksum/rcksum.ae"
                    continue;
                }
            }
if (z->seq_matches > 1) {
                {
#line 482 "rcksum/rcksum.ae"
e1 = rcksum_entry_at(z, (id + 1));
if (e1->rsum_a != (checksums_rsum_a(z->r1) & z->rsum_a_mask)) {
                        {
#line 484 "rcksum/rcksum.ae"
                            continue;
                        }
                    }
if (e1->rsum_b != checksums_rsum_b(z->r1)) {
                        {
#line 487 "rcksum/rcksum.ae"
                            continue;
                        }
                    }
                }
            }
#line 490 "rcksum/rcksum.ae"
(z->stats->weak_hit = (z->stats->weak_hit + 1));
#line 493 "rcksum/rcksum.ae"
matching = 0;
#line 494 "rcksum/rcksum.ae"
checkmd4 = 0;
while (checkmd4 < z->seq_matches) {
                {
if (checkmd4 >= cached) {
                        {
#line 497 "rcksum/rcksum.ae"
boff = (doff + (checkmd4 * z->block_size));
if ((boff + z->block_size) > data_len) {
                                {
#line 499 "rcksum/rcksum.ae"
                                    break;
                                }
                            }
#line 501 "rcksum/rcksum.ae"
{ const char* _tmp_old = block; block = fileio_slice(data, data_len, boff, z->block_size); if (_heap_block) aether_heap_str_free(_tmp_old); _heap_block = 1; }
#line 502 "rcksum/rcksum.ae"
{ const char* _tmp_old = md; md = checksums_calc_checksum(block, z->block_size); if (_heap_md) aether_heap_str_free(_tmp_old); _heap_md = 0; }
if (checkmd4 == 0) {
                                {
#line 504 "rcksum/rcksum.ae"
{ const char* _tmp_old = md_cache0; md_cache0 = aether_uniform_heap_str(md, 0); if (_heap_md_cache0) aether_heap_str_free(_tmp_old); _heap_md_cache0 = 1; }
                                }
                            } else {
                                {
#line 506 "rcksum/rcksum.ae"
{ const char* _tmp_old = md_cache1; md_cache1 = aether_uniform_heap_str(md, 0); if (_heap_md_cache1) aether_heap_str_free(_tmp_old); _heap_md_cache1 = 1; }
                                }
                            }
#line 508 "rcksum/rcksum.ae"
cached = (cached + 1);
#line 509 "rcksum/rcksum.ae"
(z->stats->checksummed = (z->stats->checksummed + 1));
                        }
                    }
#line 511 "rcksum/rcksum.ae"
{ const char* _tmp_old = cand; cand = md_cache0; if (_heap_cand) aether_heap_str_free(_tmp_old); _heap_cand = _heap_md_cache0; _heap_md_cache0 = 0; }
if (checkmd4 == 1) {
                        {
#line 513 "rcksum/rcksum.ae"
{ const char* _tmp_old = cand; cand = md_cache1; if (_heap_cand) aether_heap_str_free(_tmp_old); _heap_cand = _heap_md_cache1; _heap_md_cache1 = 0; }
                        }
                    }
#line 515 "rcksum/rcksum.ae"
{ const char* _tmp_old = stored; stored = rcksum_entry_at(z, (id + checkmd4))->md4; if (_heap_stored) aether_heap_str_free(_tmp_old); _heap_stored = 0; }
if (rcksum_prefix_eq(cand, 16, stored, 16, z->checksum_bytes) == 1) {
                        {
#line 517 "rcksum/rcksum.ae"
matching = (matching + 1);
                        }
                    } else {
                        {
#line 519 "rcksum/rcksum.ae"
                            break;
                        }
                    }
#line 521 "rcksum/rcksum.ae"
checkmd4 = (checkmd4 + 1);
                }
            }
if (matching < z->seq_matches) {
                {
#line 525 "rcksum/rcksum.ae"
                    continue;
                }
            }
#line 528 "rcksum/rcksum.ae"
(z->stats->strong_hit = (z->stats->strong_hit + matching));
#line 529 "rcksum/rcksum.ae"
next_known = ranges_next_contained_after(z->known, id);
if (next_known == (-(1))) {
                {
#line 531 "rcksum/rcksum.ae"
next_known = z->blocks;
                }
            }
#line 533 "rcksum/rcksum.ae"
num_write = matching;
if (next_known < (id + matching)) {
                {
#line 535 "rcksum/rcksum.ae"
num_write = (next_known - id);
                }
            }
#line 538 "rcksum/rcksum.ae"
span_len = (num_write * z->block_size);
#line 539 "rcksum/rcksum.ae"
{ const char* _tmp_old = prefix; prefix = fileio_slice(data, data_len, doff, span_len); if (_heap_prefix) aether_heap_str_free(_tmp_old); _heap_prefix = 1; }
#line 540 "rcksum/rcksum.ae"
next = rcksum_write_blocks(z, prefix, span_len, id, ((id + num_write) - 1), next);
#line 541 "rcksum/rcksum.ae"
got = (got + num_write);
        }
    }
#line 543 "rcksum/rcksum.ae"
    int _builder_ret = got;
    /* deferred */ if (_heap_prefix) { aether_heap_str_free(prefix); prefix = NULL; _heap_prefix = 0; }
    /* deferred */ if (_heap_stored) { aether_heap_str_free(stored); stored = NULL; _heap_stored = 0; }
    /* deferred */ if (_heap_cand) { aether_heap_str_free(cand); cand = NULL; _heap_cand = 0; }
    /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
    /* deferred */ if (_heap_block) { aether_heap_str_free(block); block = NULL; _heap_block = 0; }
    /* deferred */ if (_heap_md_cache1) { aether_heap_str_free(md_cache1); md_cache1 = NULL; _heap_md_cache1 = 0; }
    /* deferred */ if (_heap_md_cache0) { aether_heap_str_free(md_cache0); md_cache0 = NULL; _heap_md_cache0 = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_prefix) { aether_heap_str_free(prefix); prefix = NULL; _heap_prefix = 0; }
    /* deferred */ if (_heap_stored) { aether_heap_str_free(stored); stored = NULL; _heap_stored = 0; }
    /* deferred */ if (_heap_cand) { aether_heap_str_free(cand); cand = NULL; _heap_cand = 0; }
    /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
    /* deferred */ if (_heap_block) { aether_heap_str_free(block); block = NULL; _heap_block = 0; }
    /* deferred */ if (_heap_md_cache1) { aether_heap_str_free(md_cache1); md_cache1 = NULL; _heap_md_cache1 = 0; }
    /* deferred */ if (_heap_md_cache0) { aether_heap_str_free(md_cache0); md_cache0 = NULL; _heap_md_cache0 = 0; }
}

#line 549 "rcksum/rcksum.ae"
static int rcksum_submit_source_data(RcksumState* z, const char* data, int data_len, int offset) {
#line 550 "rcksum/rcksum.ae"
int x = 0;
#line 551 "rcksum/rcksum.ae"
int got = 0;
#line 552 "rcksum/rcksum.ae"
int x_limit = (data_len - z->context);
if (offset != 0) {
        {
#line 555 "rcksum/rcksum.ae"
x = z->skip;
        }
    }
#line 557 "rcksum/rcksum.ae"
(z->skip = 0);
if (x != 0) {
        {
#line 560 "rcksum/rcksum.ae"
(z->r0 = checksums_rsum_as_ptr(checksums_calc_rsum_block(data, data_len, x, z->block_size)));
if (z->seq_matches > 1) {
                {
#line 562 "rcksum/rcksum.ae"
(z->r1 = checksums_rsum_as_ptr(checksums_calc_rsum_block(data, data_len, (x + z->block_size), z->block_size)));
                }
            }
        }
    } else {
if (offset == 0) {
            {
#line 565 "rcksum/rcksum.ae"
(z->r0 = checksums_rsum_as_ptr(checksums_calc_rsum_block(data, data_len, x, z->block_size)));
if (z->seq_matches > 1) {
                    {
#line 567 "rcksum/rcksum.ae"
(z->r1 = checksums_rsum_as_ptr(checksums_calc_rsum_block(data, data_len, (x + z->block_size), z->block_size)));
                    }
                }
            }
        }
    }
    int blocks_matched;
    int thismatch;
    int old_c;
    int new_c;
    int old1;
    int new1;
while (x < x_limit) {
        {
#line 572 "rcksum/rcksum.ae"
blocks_matched = 0;
while (blocks_matched == 0) {
                {
if (x >= x_limit) {
                        {
#line 575 "rcksum/rcksum.ae"
                            break;
                        }
                    }
#line 577 "rcksum/rcksum.ae"
thismatch = rcksum_match_block(z, data, data_len, x);
if (thismatch > 0) {
                        {
#line 579 "rcksum/rcksum.ae"
blocks_matched = z->seq_matches;
#line 580 "rcksum/rcksum.ae"
got = (got + thismatch);
                        }
                    }
if (blocks_matched == 0) {
                        {
if ((x + (z->block_size * z->seq_matches)) < data_len) {
                                {
#line 584 "rcksum/rcksum.ae"
old_c = (string_char_at_n(data, data_len, x) & 0xff);
#line 585 "rcksum/rcksum.ae"
new_c = (string_char_at_n(data, data_len, (x + z->block_size)) & 0xff);
#line 586 "rcksum/rcksum.ae"
checksums_update_rsum(z->r0, old_c, new_c, z->block_shift);
if (z->seq_matches > 1) {
                                        {
#line 588 "rcksum/rcksum.ae"
old1 = (string_char_at_n(data, data_len, (x + z->block_size)) & 0xff);
#line 589 "rcksum/rcksum.ae"
new1 = (string_char_at_n(data, data_len, (x + (2 * z->block_size))) & 0xff);
#line 590 "rcksum/rcksum.ae"
checksums_update_rsum(z->r1, old1, new1, z->block_shift);
                                        }
                                    }
                                }
                            }
#line 593 "rcksum/rcksum.ae"
x = (x + 1);
                        }
                    }
                }
            }
if (blocks_matched > 0) {
                {
#line 598 "rcksum/rcksum.ae"
x = (x + (z->block_size * blocks_matched));
if (x <= x_limit) {
                        {
if (z->seq_matches > 1) {
                                {
if (blocks_matched == 1) {
                                        {
#line 602 "rcksum/rcksum.ae"
(z->r0 = z->r1);
                                        }
                                    } else {
if ((x + z->block_size) <= data_len) {
                                            {
#line 604 "rcksum/rcksum.ae"
(z->r0 = checksums_rsum_as_ptr(checksums_calc_rsum_block(data, data_len, x, z->block_size)));
                                            }
                                        }
                                    }
                                }
                            } else {
if ((x + z->block_size) <= data_len) {
                                    {
#line 607 "rcksum/rcksum.ae"
(z->r0 = checksums_rsum_as_ptr(checksums_calc_rsum_block(data, data_len, x, z->block_size)));
                                    }
                                }
                            }
if (z->seq_matches > 1) {
                                {
if ((x + (2 * z->block_size)) <= data_len) {
                                        {
#line 611 "rcksum/rcksum.ae"
(z->r1 = checksums_rsum_as_ptr(checksums_calc_rsum_block(data, data_len, (x + z->block_size), z->block_size)));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
#line 618 "rcksum/rcksum.ae"
(z->skip = (x - x_limit));
#line 619 "rcksum/rcksum.ae"
    return got;
}

#line 629 "rcksum/rcksum.ae"
static int rcksum_submit_source_buffer(RcksumState* z, const char* data, int data_len) {
    int _heap_padded = 0; (void)_heap_padded;
    const char* padded = NULL;
    int _heap_zeros = 0; (void)_heap_zeros;
    const char* zeros = NULL;
if (z->rsum_hash == NULL) {
        {
#line 631 "rcksum/rcksum.ae"
rcksum_build_hash(z);
        }
    }
#line 633 "rcksum/rcksum.ae"
int buf_size = (z->block_size * 16);
#line 637 "rcksum/rcksum.ae"
int eff = data_len;
if (data_len < buf_size) {
        {
#line 639 "rcksum/rcksum.ae"
eff = (data_len + z->context);
if (eff > buf_size) {
                {
#line 641 "rcksum/rcksum.ae"
eff = buf_size;
                }
            }
        }
    }
#line 645 "rcksum/rcksum.ae"
{ const char* _tmp_old = padded; padded = data; if (_heap_padded) aether_heap_str_free(_tmp_old); _heap_padded = 0; }
if (eff > data_len) {
        {
#line 647 "rcksum/rcksum.ae"
{ const char* _tmp_old = zeros; zeros = rcksum_make_zeros((eff - data_len)); if (_heap_zeros) aether_heap_str_free(_tmp_old); _heap_zeros = 1; }
#line 648 "rcksum/rcksum.ae"
{ const char* _tmp_old = padded; padded = rcksum_append_bytes(data, data_len, zeros, (eff - data_len)); if (_heap_padded) aether_heap_str_free(_tmp_old); _heap_padded = 1; }
        }
    }
#line 650 "rcksum/rcksum.ae"
    int _builder_ret = rcksum_submit_source_data(z, padded, eff, 0);
    /* deferred */ if (_heap_zeros) { aether_heap_str_free(zeros); zeros = NULL; _heap_zeros = 0; }
    /* deferred */ if (_heap_padded) { aether_heap_str_free(padded); padded = NULL; _heap_padded = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_zeros) { aether_heap_str_free(zeros); zeros = NULL; _heap_zeros = 0; }
    /* deferred */ if (_heap_padded) { aether_heap_str_free(padded); padded = NULL; _heap_padded = 0; }
}

#line 654 "rcksum/rcksum.ae"
static const char* rcksum_make_zeros(int n) {
#line 655 "rcksum/rcksum.ae"
    return aether_uniform_heap_str((const char*)(fileio_buf_alloc_str(n)), 1);
}

#line 660 "rcksum/rcksum.ae"
static const char* rcksum_append_bytes(const char* a, int a_len, const char* b, int b_len) {
#line 661 "rcksum/rcksum.ae"
int total = (a_len + b_len);
#line 662 "rcksum/rcksum.ae"
void* out = fileio_buf_alloc(total);
#line 663 "rcksum/rcksum.ae"
int i = 0;
while (i < a_len) {
        {
#line 665 "rcksum/rcksum.ae"
fileio_buf_set(out, i, (string_char_at_n(a, a_len, i) & 0xff));
#line 666 "rcksum/rcksum.ae"
i = (i + 1);
        }
    }
#line 668 "rcksum/rcksum.ae"
int j = 0;
while (j < b_len) {
        {
#line 670 "rcksum/rcksum.ae"
fileio_buf_set(out, (a_len + j), (string_char_at_n(b, b_len, j) & 0xff));
#line 671 "rcksum/rcksum.ae"
j = (j + 1);
        }
    }
#line 673 "rcksum/rcksum.ae"
    return aether_uniform_heap_str((const char*)(fileio_buf_to_str(out, total)), 1);
}

#line 677 "rcksum/rcksum.ae"
static _tuple_string_int rcksum_read_known_data(RcksumState* z, int len, int offset) {
if (z->fd < 0) {
        {
#line 679 "rcksum/rcksum.ae"
            return (_tuple_string_int){aether_uniform_heap_str((const char*)(""), 0), 0};
        }
    }
#line 681 "rcksum/rcksum.ae"
    return fileio_read_at(z->fd, len, offset);
}

// Import: rcksum.checksums
#line 32 "rcksum/checksums.ae"
static RSum* checksums_new_rsum(int a, int b) {
#line 33 "rcksum/checksums.ae"
void* raw = malloc(16);
#line 34 "rcksum/checksums.ae"
RSum* r = ((RSum*)(raw));
#line 35 "rcksum/checksums.ae"
(r->a = (a & 0xffff));
#line 36 "rcksum/checksums.ae"
(r->b = (b & 0xffff));
#line 37 "rcksum/checksums.ae"
    return r;
}

#line 42 "rcksum/checksums.ae"
static void* checksums_rsum_as_ptr(RSum* r) {
#line 43 "rcksum/checksums.ae"
    return r;
}

#line 46 "rcksum/checksums.ae"
static int checksums_rsum_a(RSum* r) {
#line 47 "rcksum/checksums.ae"
    return r->a;
}

#line 50 "rcksum/checksums.ae"
static int checksums_rsum_b(RSum* r) {
#line 51 "rcksum/checksums.ae"
    return r->b;
}

#line 58 "rcksum/checksums.ae"
static RSum* checksums_calc_rsum_block(const char* data, int data_len, int off, int len) {
#line 59 "rcksum/checksums.ae"
int a = 0;
#line 60 "rcksum/checksums.ae"
int b = 0;
#line 61 "rcksum/checksums.ae"
int i = 0;
    int c;
while (i < len) {
        {
#line 63 "rcksum/checksums.ae"
c = checksums_byte_at(data, data_len, (off + i));
#line 64 "rcksum/checksums.ae"
a = ((a + c) & 0xffff);
#line 65 "rcksum/checksums.ae"
b = ((b + a) & 0xffff);
#line 66 "rcksum/checksums.ae"
i = (i + 1);
        }
    }
#line 68 "rcksum/checksums.ae"
    return checksums_new_rsum(a, b);
}

#line 75 "rcksum/checksums.ae"
static void checksums_update_rsum(RSum* r, int old_c, int new_c, int block_shift) {
#line 76 "rcksum/checksums.ae"
(r->a = (((r->a + new_c) - old_c) & 0xffff));
#line 77 "rcksum/checksums.ae"
(r->b = (((r->b + r->a) - ((old_c << block_shift) & 0xffff)) & 0xffff));
}

#line 83 "rcksum/checksums.ae"
static const char* checksums_calc_checksum(const char* data, int len) {
    int _heap_digest = 0; (void)_heap_digest;
    const char* digest = NULL;
    int _heap_err = 0; (void)_heap_err;
    const char* err = NULL;
#line 84 "rcksum/checksums.ae"
    _tuple_string_int_string _tup8 = cryptography_md4_bytes(data, len);
    { const char* _tmp_old = digest; digest = _tup8._0; if (_heap_digest) aether_heap_str_free(_tmp_old); _heap_digest = 1; }
    int n = _tup8._1;
    { const char* _tmp_old = err; err = _tup8._2; if (_heap_err) aether_heap_str_free(_tmp_old); _heap_err = 0; }
if (strcmp(_aether_safe_str(err), _aether_safe_str("")) != 0) {
        {
#line 86 "rcksum/checksums.ae"
            const char* _builder_ret = "";
            if (_heap_digest) { aether_heap_str_free(digest); digest = NULL; _heap_digest = 0; }
            /* deferred */ if (_heap_err) { aether_heap_str_free(err); err = NULL; _heap_err = 0; }
            return _builder_ret;
        }
    }
#line 88 "rcksum/checksums.ae"
    const char* _builder_ret = digest;
    /* deferred */ if (_heap_err) { aether_heap_str_free(err); err = NULL; _heap_err = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_err) { aether_heap_str_free(err); err = NULL; _heap_err = 0; }
}

#line 95 "rcksum/checksums.ae"
static int checksums_byte_at(const char* data, int data_len, int i) {
#line 96 "rcksum/checksums.ae"
    return (string_char_at_n(data, data_len, i) & 0xff);
}

#line 104 "rcksum/checksums.ae"
static int checksums_calc_rhash_from_rsums(RSum* rs1, RSum* rs2, int seq_matches, int rsum_a_mask) {
#line 105 "rcksum/checksums.ae"
int hash = rs1->b;
if (seq_matches > 1) {
        {
#line 107 "rcksum/checksums.ae"
hash = (hash ^ ((rs2->b << 16) & 0xffffffff));
        }
    } else {
        {
#line 109 "rcksum/checksums.ae"
hash = (hash ^ (((rs1->a & rsum_a_mask) << 16) & 0xffffffff));
        }
    }
#line 111 "rcksum/checksums.ae"
    return (hash & 0xffffffff);
}

#line 116 "rcksum/checksums.ae"
static int checksums_log2(int x) {
if (x == 0) {
        {
#line 118 "rcksum/checksums.ae"
            return 0;
        }
    }
#line 120 "rcksum/checksums.ae"
int n = 0;
#line 121 "rcksum/checksums.ae"
int64_t v = (x & 0xffffffff);
while (v > 1) {
        {
#line 123 "rcksum/checksums.ae"
v = (v >> 1);
#line 124 "rcksum/checksums.ae"
n = (n + 1);
        }
    }
#line 126 "rcksum/checksums.ae"
    return n;
}

// Import: std.cryptography
#line 80 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
static _tuple_string_string cryptography_sha1_hex(const char* data, int length) {
    int _heap_out = 0; (void)_heap_out;
    const char* out = NULL;
#line 81 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
{ const char* _tmp_old = out; out = cryptography_sha1_hex_raw(aether_string_data(data), length); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 0; }
if (out == 0) {
        {
#line 83 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
            return (_tuple_string_string){aether_uniform_heap_str((const char*)(""), 0), "openssl unavailable"};
        }
    }
#line 85 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
    return (_tuple_string_string){aether_uniform_heap_str((const char*)(out), _heap_out), ""};
}

#line 151 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
static _tuple_string_string cryptography_base64_encode_padded(const char* data, int length) {
    int _heap_out = 0; (void)_heap_out;
    const char* out = NULL;
#line 152 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
{ const char* _tmp_old = out; out = cryptography_base64_encode_padded_raw(aether_string_data(data), length); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 0; }
if (out == 0) {
        {
#line 154 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
            return (_tuple_string_string){"", "openssl unavailable"};
        }
    }
#line 156 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
    return (_tuple_string_string){out, ""};
}

#line 244 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
static _tuple_string_int_string cryptography_md4_bytes(const char* data, int length) {
    int _heap_raw = 0; (void)_heap_raw;
    const char* raw = NULL;
    int _heap_owned = 0; (void)_heap_owned;
    const char* owned = NULL;
#line 245 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
int ok = cryptography_md4_bytes_raw(aether_string_data(data), length);
if (ok == 0) {
        {
#line 247 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
            _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(""), 0), 0, "md4 unavailable"};
            /* deferred */ if (_heap_raw) { aether_heap_str_free(raw); raw = NULL; _heap_raw = 0; }
            return _builder_ret;
        }
    }
#line 249 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
{ const char* _tmp_old = raw; raw = cryptography_get_binary_digest(); if (_heap_raw) aether_heap_str_free(_tmp_old); _heap_raw = 0; }
#line 250 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
int n = cryptography_get_binary_digest_length();
#line 251 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
{ const char* _tmp_old = owned; owned = string_new_with_length(raw, n); if (_heap_owned) aether_heap_str_free(_tmp_old); _heap_owned = 1; }
#line 252 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
cryptography_release_binary_digest();
#line 253 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
    _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(owned), _heap_owned), n, ""};
    /* deferred */ if (_heap_raw) { aether_heap_str_free(raw); raw = NULL; _heap_raw = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_raw) { aether_heap_str_free(raw); raw = NULL; _heap_raw = 0; }
}

// Import: std.map
// Import: rcksum.ranges
#line 27 "rcksum/ranges.ae"
static BlockPair* ranges_new_pair(int start, int fin) {
#line 28 "rcksum/ranges.ae"
void* raw = malloc(16);
#line 29 "rcksum/ranges.ae"
BlockPair* p = ((BlockPair*)(raw));
#line 30 "rcksum/ranges.ae"
(p->start = start);
#line 31 "rcksum/ranges.ae"
(p->fin = fin);
#line 32 "rcksum/ranges.ae"
    return p;
}

#line 38 "rcksum/ranges.ae"
static int ranges_pair_start(BlockPair* p) {
#line 39 "rcksum/ranges.ae"
    return p->start;
}

#line 42 "rcksum/ranges.ae"
static int ranges_pair_fin(BlockPair* p) {
#line 43 "rcksum/ranges.ae"
    return p->fin;
}

#line 46 "rcksum/ranges.ae"
static BlockRanges* ranges_new_ranges(void) {
#line 47 "rcksum/ranges.ae"
void* raw = malloc(16);
#line 48 "rcksum/ranges.ae"
BlockRanges* z = ((BlockRanges*)(raw));
#line 49 "rcksum/ranges.ae"
(z->ranges = list_new());
#line 50 "rcksum/ranges.ae"
(z->got_blocks = 0);
#line 51 "rcksum/ranges.ae"
    return z;
}

#line 55 "rcksum/ranges.ae"
static void* ranges_ranges_as_ptr(BlockRanges* z) {
#line 56 "rcksum/ranges.ae"
    return z;
}

#line 64 "rcksum/ranges.ae"
static int ranges_got_blocks(BlockRanges* z) {
#line 65 "rcksum/ranges.ae"
    return z->got_blocks;
}

#line 74 "rcksum/ranges.ae"
static int ranges_range_start_at(BlockRanges* z, int i) {
#line 75 "rcksum/ranges.ae"
    return ranges_pair_start(ranges_pair_at(z, i));
}

#line 78 "rcksum/ranges.ae"
static int ranges_range_fin_at(BlockRanges* z, int i) {
#line 79 "rcksum/ranges.ae"
    return ranges_pair_fin(ranges_pair_at(z, i));
}

#line 82 "rcksum/ranges.ae"
static BlockPair* ranges_pair_at(BlockRanges* z, int i) {
#line 83 "rcksum/ranges.ae"
    return ((BlockPair*)(list_get_raw(z->ranges, i)));
}

#line 89 "rcksum/ranges.ae"
static void ranges_insert_pair(BlockRanges* z, int at, BlockPair* p) {
#line 90 "rcksum/ranges.ae"
int n = list_size(z->ranges);
if (at >= n) {
        {
#line 92 "rcksum/ranges.ae"
list_add_raw(z->ranges, p);
#line 93 "rcksum/ranges.ae"
            return;
        }
    }
#line 96 "rcksum/ranges.ae"
void* last = list_get_raw(z->ranges, (n - 1));
#line 97 "rcksum/ranges.ae"
list_add_raw(z->ranges, last);
#line 99 "rcksum/ranges.ae"
int i = (n - 1);
    void* prev;
while (i > at) {
        {
#line 101 "rcksum/ranges.ae"
prev = list_get_raw(z->ranges, (i - 1));
#line 102 "rcksum/ranges.ae"
list_set(z->ranges, i, prev);
#line 103 "rcksum/ranges.ae"
i = (i - 1);
        }
    }
#line 105 "rcksum/ranges.ae"
list_set(z->ranges, at, p);
}

#line 113 "rcksum/ranges.ae"
static int ranges_range_before_block(BlockRanges* z, int x) {
#line 114 "rcksum/ranges.ae"
int lo = 0;
#line 115 "rcksum/ranges.ae"
int hi = (list_size(z->ranges) - 1);
    int r;
    BlockPair* p;
while (lo <= hi) {
        {
#line 117 "rcksum/ranges.ae"
r = ((hi + lo) / 2);
#line 118 "rcksum/ranges.ae"
p = ranges_pair_at(z, r);
if (x > p->fin) {
                {
#line 120 "rcksum/ranges.ae"
lo = (r + 1);
                }
            } else {
if (x < p->start) {
                    {
#line 122 "rcksum/ranges.ae"
hi = (r - 1);
                    }
                } else {
                    {
#line 124 "rcksum/ranges.ae"
                        return (-(1));
                    }
                }
            }
        }
    }
#line 127 "rcksum/ranges.ae"
    return lo;
}

#line 131 "rcksum/ranges.ae"
static void ranges_add_to_ranges(BlockRanges* z, int x) {
#line 132 "rcksum/ranges.ae"
int r = ranges_range_before_block(z, x);
if (r == (-(1))) {
        {
#line 134 "rcksum/ranges.ae"
            return;
        }
    }
#line 136 "rcksum/ranges.ae"
(z->got_blocks = (z->got_blocks + 1));
#line 137 "rcksum/ranges.ae"
int n = list_size(z->ranges);
if (r > 0) {
        {
if (r < n) {
                {
#line 142 "rcksum/ranges.ae"
BlockPair* below = ranges_pair_at(z, (r - 1));
#line 143 "rcksum/ranges.ae"
BlockPair* above = ranges_pair_at(z, r);
if (below->fin == (x - 1)) {
                        {
if (above->start == (x + 1)) {
                                {
#line 146 "rcksum/ranges.ae"
(below->fin = above->fin);
#line 147 "rcksum/ranges.ae"
list_remove(z->ranges, r);
#line 148 "rcksum/ranges.ae"
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
if (r > 0) {
        {
if (n > 0) {
                {
#line 157 "rcksum/ranges.ae"
BlockPair* below = ranges_pair_at(z, (r - 1));
if (below->fin == (x - 1)) {
                        {
#line 159 "rcksum/ranges.ae"
(below->fin = x);
#line 160 "rcksum/ranges.ae"
                            return;
                        }
                    }
                }
            }
        }
    }
if (r < n) {
        {
#line 167 "rcksum/ranges.ae"
BlockPair* above = ranges_pair_at(z, r);
if (above->start == (x + 1)) {
                {
#line 169 "rcksum/ranges.ae"
(above->start = x);
#line 170 "rcksum/ranges.ae"
                    return;
                }
            }
        }
    }
#line 175 "rcksum/ranges.ae"
ranges_insert_pair(z, r, ranges_new_pair(x, x));
}

#line 179 "rcksum/ranges.ae"
static int ranges_contains(BlockRanges* z, int x) {
if (ranges_range_before_block(z, x) == (-(1))) {
        {
#line 181 "rcksum/ranges.ae"
            return 1;
        }
    }
#line 183 "rcksum/ranges.ae"
    return 0;
}

#line 188 "rcksum/ranges.ae"
static int ranges_next_contained_after(BlockRanges* z, int x) {
#line 189 "rcksum/ranges.ae"
int r = ranges_range_before_block(z, x);
#line 190 "rcksum/ranges.ae"
int n = list_size(z->ranges);
if (r == (-(1))) {
        {
#line 193 "rcksum/ranges.ae"
int i = 0;
            BlockPair* p;
while (i < n) {
                {
#line 195 "rcksum/ranges.ae"
p = ranges_pair_at(z, i);
if (x >= p->start) {
                        {
if (x <= p->fin) {
                                {
#line 198 "rcksum/ranges.ae"
                                    return (p->fin + 1);
                                }
                            }
                        }
                    }
#line 201 "rcksum/ranges.ae"
i = (i + 1);
                }
            }
        }
    }
if (r >= n) {
        {
#line 205 "rcksum/ranges.ae"
            return (-(1));
        }
    }
#line 207 "rcksum/ranges.ae"
    return ranges_pair_at(z, r)->start;
}

#line 211 "rcksum/ranges.ae"
static int ranges_list_len(void* l) {
#line 212 "rcksum/ranges.ae"
    return list_size(l);
}

#line 216 "rcksum/ranges.ae"
static BlockPair* ranges_list_pair(void* l, int i) {
#line 217 "rcksum/ranges.ae"
    return ((BlockPair*)(list_get_raw(l, i)));
}

#line 223 "rcksum/ranges.ae"
static void* ranges_missing_blocks_between(BlockRanges* z, int from, int to) {
#line 224 "rcksum/ranges.ae"
void* result = list_new();
if (from > to) {
        {
#line 226 "rcksum/ranges.ae"
            return result;
        }
    }
if (from < 0) {
        {
#line 229 "rcksum/ranges.ae"
from = 0;
        }
    }
#line 231 "rcksum/ranges.ae"
int current = from;
#line 232 "rcksum/ranges.ae"
int n = list_size(z->ranges);
#line 233 "rcksum/ranges.ae"
int i = 0;
    BlockPair* p;
    int range_start;
    int range_end;
    int gap_end;
while (i < n) {
        {
#line 235 "rcksum/ranges.ae"
p = ranges_pair_at(z, i);
#line 236 "rcksum/ranges.ae"
range_start = p->start;
#line 237 "rcksum/ranges.ae"
range_end = p->fin;
if (current < range_start) {
                {
#line 239 "rcksum/ranges.ae"
gap_end = range_start;
if (gap_end > to) {
                        {
#line 241 "rcksum/ranges.ae"
gap_end = (to + 1);
                        }
                    }
#line 243 "rcksum/ranges.ae"
list_add_raw(result, ranges_new_pair(current, (gap_end - 1)));
#line 244 "rcksum/ranges.ae"
current = (range_end + 1);
                }
            } else {
if (current <= range_end) {
                    {
#line 246 "rcksum/ranges.ae"
current = (range_end + 1);
                    }
                }
            }
#line 248 "rcksum/ranges.ae"
i = (i + 1);
        }
    }
if (current <= to) {
        {
#line 251 "rcksum/ranges.ae"
list_add_raw(result, ranges_new_pair(current, to));
        }
    }
#line 253 "rcksum/ranges.ae"
    return result;
}

int main(int argc, char** argv) {
    #ifdef _WIN32
    SetConsoleOutputCP(65001);  // CP_UTF8
    SetConsoleCP(65001);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    #endif
    aether_args_init(argc, argv);
    
    int _heap_out = 0; (void)_heap_out;
    const char* out = NULL;
    int _heap_auth_creds = 0; (void)_heap_auth_creds;
    const char* auth_creds = NULL;
    int _heap_keep = 0; (void)_heap_keep;
    const char* keep = NULL;
    int _heap_source = 0; (void)_heap_source;
    const char* source = NULL;
    int _heap_a = 0; (void)_heap_a;
    const char* a = NULL;
    int _heap_auth = 0; (void)_heap_auth;
    const char* auth = NULL;
    int _heap_cbytes = 0; (void)_heap_cbytes;
    const char* cbytes = NULL;
    int _heap_cerr = 0; (void)_heap_cerr;
    const char* cerr = NULL;
    int _heap_sf = 0; (void)_heap_sf;
    const char* sf = NULL;
    int _heap_ferr = 0; (void)_heap_ferr;
    const char* ferr = NULL;
    int _heap_cerr2 = 0; (void)_heap_cerr2;
    const char* cerr2 = NULL;
    int _heap_s = 0; (void)_heap_s;
    const char* s = NULL;
    {
#line 27 "cmd/zsync.ae"
int argc = aether_args_count();
#line 28 "cmd/zsync.ae"
{ const char* _tmp_old = out; out = ""; if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 0; }
#line 29 "cmd/zsync.ae"
void* seeds = list_new();
#line 30 "cmd/zsync.ae"
{ const char* _tmp_old = auth_creds; auth_creds = ""; if (_heap_auth_creds) aether_heap_str_free(_tmp_old); _heap_auth_creds = 0; }
#line 31 "cmd/zsync.ae"
{ const char* _tmp_old = keep; keep = ""; if (_heap_keep) aether_heap_str_free(_tmp_old); _heap_keep = 0; }
#line 32 "cmd/zsync.ae"
int quiet = 0;
#line 33 "cmd/zsync.ae"
int verbose = 0;
#line 34 "cmd/zsync.ae"
{ const char* _tmp_old = source; source = ""; if (_heap_source) aether_heap_str_free(_tmp_old); _heap_source = 0; }
#line 36 "cmd/zsync.ae"
int i = 1;
while (i < argc) {
            {
#line 38 "cmd/zsync.ae"
{ const char* _tmp_old = a; a = aether_args_get(i); if (_heap_a) aether_heap_str_free(_tmp_old); _heap_a = 0; }
if (string_equals(a, "-o") == 1) {
                    {
#line 40 "cmd/zsync.ae"
i = (i + 1);
#line 41 "cmd/zsync.ae"
{ const char* _tmp_old = out; out = aether_args_get(i); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 0; }
                    }
                } else {
if (string_equals(a, "-i") == 1) {
                        {
#line 43 "cmd/zsync.ae"
i = (i + 1);
#line 44 "cmd/zsync.ae"
list_add_string_owned(seeds, (void*)str_owned(aether_args_get(i)));
                        }
                    } else {
if (string_equals(a, "-A") == 1) {
                            {
#line 46 "cmd/zsync.ae"
i = (i + 1);
#line 47 "cmd/zsync.ae"
{ const char* _tmp_old = auth_creds; auth_creds = creds_of(aether_args_get(i)); if (_heap_auth_creds) aether_heap_str_free(_tmp_old); _heap_auth_creds = 1; }
                            }
                        } else {
if (string_equals(a, "-k") == 1) {
                                {
#line 49 "cmd/zsync.ae"
i = (i + 1);
#line 50 "cmd/zsync.ae"
{ const char* _tmp_old = keep; keep = aether_args_get(i); if (_heap_keep) aether_heap_str_free(_tmp_old); _heap_keep = 0; }
                                }
                            } else {
if (string_equals(a, "-q") == 1) {
                                    {
#line 52 "cmd/zsync.ae"
quiet = 1;
                                    }
                                } else {
if (string_equals(a, "-v") == 1) {
                                        {
#line 54 "cmd/zsync.ae"
verbose = 1;
                                        }
                                    } else {
if (string_equals(a, "-V") == 1) {
                                            {
#line 56 "cmd/zsync.ae"
printf("zsync v%s", _aether_safe_str(VERSION())); putchar('\n');
#line 57 "cmd/zsync.ae"
exit(0);
                                            }
                                        } else {
if (string_equals(a, "--no-check-certificate") == 1) {
                                                {
                                                }
                                            } else {
                                                {
#line 63 "cmd/zsync.ae"
{ const char* _tmp_old = source; source = aether_uniform_heap_str(a, 0); if (_heap_source) aether_heap_str_free(_tmp_old); _heap_source = 1; }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
#line 65 "cmd/zsync.ae"
i = (i + 1);
            }
        }
if (string_equals(source, "") == 1) {
            {
#line 69 "cmd/zsync.ae"
puts("usage: zsync [-o out] [-i seed]... [-A host=user:pass] [-k keep] [-q] [-v] [-V] <.zsync>");
#line 70 "cmd/zsync.ae"
exit(3);
            }
        }
#line 73 "cmd/zsync.ae"
{ const char* _tmp_old = auth; auth = clientlib_basic_auth(auth_creds); if (_heap_auth) aether_heap_str_free(_tmp_old); _heap_auth = 1; }
#line 76 "cmd/zsync.ae"
        _tuple_string_int_string _tup9 = get_control(source, keep, auth, quiet);
        { const char* _tmp_old = cbytes; cbytes = _tup9._0; if (_heap_cbytes) aether_heap_str_free(_tmp_old); _heap_cbytes = 1; }
        int clen = _tup9._1;
        { const char* _tmp_old = cerr; cerr = _tup9._2; if (_heap_cerr) aether_heap_str_free(_tmp_old); _heap_cerr = 0; }
if (strcmp(_aether_safe_str(cerr), _aether_safe_str("")) != 0) {
            {
#line 78 "cmd/zsync.ae"
printf("failed to fetch control file: %s", _aether_safe_str(cerr)); putchar('\n');
#line 79 "cmd/zsync.ae"
exit(3);
            }
        }
#line 81 "cmd/zsync.ae"
Control* ctl = control_parse(cbytes, clen);
if (control_ctl_ok(ctl) == 0) {
            {
#line 83 "cmd/zsync.ae"
printf("failed to parse control file: %s", _aether_safe_str(control_ctl_err(ctl))); putchar('\n');
#line 84 "cmd/zsync.ae"
exit(3);
            }
        }
if (string_equals(out, "") == 1) {
            {
#line 89 "cmd/zsync.ae"
{ const char* _tmp_old = out; out = control_ctl_filename(ctl); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 0; }
if (string_equals(out, "") == 1) {
                    {
#line 91 "cmd/zsync.ae"
{ const char* _tmp_old = out; out = "zsync-download"; if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 0; }
                    }
                }
            }
        }
#line 95 "cmd/zsync.ae"
int fd = fileio_open_rw(out);
if (fd < 0) {
            {
#line 97 "cmd/zsync.ae"
printf("cannot open output %s", _aether_safe_str(out)); putchar('\n');
#line 98 "cmd/zsync.ae"
exit(1);
            }
        }
#line 100 "cmd/zsync.ae"
State* st = download_new_state(ctl, fd);
#line 105 "cmd/zsync.ae"
feed_seed_if_exists(st, out, quiet);
#line 106 "cmd/zsync.ae"
int ns = list_size(seeds);
#line 107 "cmd/zsync.ae"
int si = 0;
while (si < ns) {
            {
#line 109 "cmd/zsync.ae"
{ const char* _tmp_old = sf; sf = fileio_buf_as_string(list_get_raw(seeds, si)); if (_heap_sf) aether_heap_str_free(_tmp_old); _heap_sf = 0; }
#line 110 "cmd/zsync.ae"
feed_seed_if_exists(st, sf, quiet);
#line 111 "cmd/zsync.ae"
si = (si + 1);
            }
        }
if (download_status(st) == 0) {
            {
if (quiet == 0) {
                    {
#line 116 "cmd/zsync.ae"
puts("No relevant local data — downloading the whole file.");
                    }
                }
            }
        }
#line 121 "cmd/zsync.ae"
{ const char* _tmp_old = ferr; ferr = fetch_from_urls(st, ctl, source, auth, quiet); if (_heap_ferr) aether_heap_str_free(_tmp_old); _heap_ferr = 0; }
if (strcmp(_aether_safe_str(ferr), _aether_safe_str("")) != 0) {
            {
#line 123 "cmd/zsync.ae"
printf("download failed: %s", _aether_safe_str(ferr)); putchar('\n');
#line 124 "cmd/zsync.ae"
exit(3);
            }
        }
#line 128 "cmd/zsync.ae"
{ const char* _tmp_old = cerr2; cerr2 = download_complete(st); if (_heap_cerr2) aether_heap_str_free(_tmp_old); _heap_cerr2 = 0; }
#line 129 "cmd/zsync.ae"
fileio_close_fd(fd);
if (strcmp(_aether_safe_str(cerr2), _aether_safe_str("")) != 0) {
            {
#line 131 "cmd/zsync.ae"
printf("verification failed: %s", _aether_safe_str(cerr2)); putchar('\n');
#line 132 "cmd/zsync.ae"
exit(2);
            }
        }
if (quiet == 0) {
            {
#line 135 "cmd/zsync.ae"
puts("verifying download...checksum matches OK");
#line 136 "cmd/zsync.ae"
                _tuple_int_int _tup10 = download_progress(st);
                int got = _tup10._0;
                int total = _tup10._1;
#line 137 "cmd/zsync.ae"
printf("download complete: %s", _aether_safe_str(out)); putchar('\n');
            }
        }
if (verbose == 1) {
            {
#line 140 "cmd/zsync.ae"
s = download_stats_line(st);
#line 141 "cmd/zsync.ae"
printf("%s\n", _aether_safe_str(s));
            }
        }
    }
    /* deferred */ if (_heap_cerr2) { aether_heap_str_free(cerr2); cerr2 = NULL; _heap_cerr2 = 0; }
    /* deferred */ if (_heap_ferr) { aether_heap_str_free(ferr); ferr = NULL; _heap_ferr = 0; }
    /* deferred */ if (_heap_sf) { aether_heap_str_free(sf); sf = NULL; _heap_sf = 0; }
    /* deferred */ if (_heap_cerr) { aether_heap_str_free(cerr); cerr = NULL; _heap_cerr = 0; }
    /* deferred */ if (_heap_cbytes) { aether_heap_str_free(cbytes); cbytes = NULL; _heap_cbytes = 0; }
    /* deferred */ if (_heap_auth) { aether_heap_str_free(auth); auth = NULL; _heap_auth = 0; }
    /* deferred */ if (_heap_a) { aether_heap_str_free(a); a = NULL; _heap_a = 0; }
    /* deferred */ if (_heap_source) { aether_heap_str_free(source); source = NULL; _heap_source = 0; }
    /* deferred */ if (_heap_keep) { aether_heap_str_free(keep); keep = NULL; _heap_keep = 0; }
    /* deferred */ if (_heap_auth_creds) { aether_heap_str_free(auth_creds); auth_creds = NULL; _heap_auth_creds = 0; }
    /* deferred */ if (_heap_out) { aether_heap_str_free(out); out = NULL; _heap_out = 0; }
    return 0;
}
#line 148 "cmd/zsync.ae"
_tuple_string_int_string get_control(const char* source, const char* keep, const char* auth, int quiet) {
    int _heap_ims = 0; (void)_heap_ims;
    const char* ims = NULL;
    int _heap_me = 0; (void)_heap_me;
    const char* me = NULL;
    int _heap_body = 0; (void)_heap_body;
    const char* body = NULL;
    int _heap_e = 0; (void)_heap_e;
    const char* e = NULL;
if (fs_exists(source) == 1) {
        {
#line 151 "cmd/zsync.ae"
            _tuple_string_int_string _builder_ret = fs_read_binary(source);
            if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
            /* deferred */ if (_heap_me) { aether_heap_str_free(me); me = NULL; _heap_me = 0; }
            /* deferred */ if (_heap_ims) { aether_heap_str_free(ims); ims = NULL; _heap_ims = 0; }
            return _builder_ret;
        }
    }
#line 153 "cmd/zsync.ae"
{ const char* _tmp_old = ims; ims = ""; if (_heap_ims) aether_heap_str_free(_tmp_old); _heap_ims = 0; }
if (string_equals(keep, "") == 0) {
        {
if (fs_exists(keep) == 1) {
                {
#line 156 "cmd/zsync.ae"
                    _tuple_int_string _tup11 = fs_mtime(keep);
                    int epoch = _tup11._0;
                    { const char* _tmp_old = me; me = _tup11._1; if (_heap_me) aether_heap_str_free(_tmp_old); _heap_me = 0; }
if (strcmp(_aether_safe_str(me), _aether_safe_str("")) == 0) {
                        {
#line 158 "cmd/zsync.ae"
{ const char* _tmp_old = ims; ims = fileio_rfc1123z(epoch); if (_heap_ims) aether_heap_str_free(_tmp_old); _heap_ims = 0; }
                        }
                    }
                }
            }
        }
    }
#line 162 "cmd/zsync.ae"
    _tuple_string_int_int_string _tup12 = clientlib_http_get_auth(source, auth, ims);
    { const char* _tmp_old = body; body = _tup12._0; if (_heap_body) aether_heap_str_free(_tmp_old); _heap_body = 1; }
    int n = _tup12._1;
    int status = _tup12._2;
    { const char* _tmp_old = e; e = _tup12._3; if (_heap_e) aether_heap_str_free(_tmp_old); _heap_e = 1; }
if (status == 304) {
        {
if (quiet == 0) {
                {
#line 165 "cmd/zsync.ae"
puts("control file not modified — using local copy");
                }
            }
#line 167 "cmd/zsync.ae"
            _tuple_string_int_string _builder_ret = fs_read_binary(keep);
            if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            if (_heap_body) { aether_heap_str_free(body); body = NULL; _heap_body = 0; }
            /* deferred */ if (_heap_me) { aether_heap_str_free(me); me = NULL; _heap_me = 0; }
            /* deferred */ if (_heap_ims) { aether_heap_str_free(ims); ims = NULL; _heap_ims = 0; }
            return _builder_ret;
        }
    }
if (strcmp(_aether_safe_str(e), _aether_safe_str("")) != 0) {
        {
#line 170 "cmd/zsync.ae"
            _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(""), 0), 0, e};
            /* deferred */ if (_heap_me) { aether_heap_str_free(me); me = NULL; _heap_me = 0; }
            /* deferred */ if (_heap_ims) { aether_heap_str_free(ims); ims = NULL; _heap_ims = 0; }
            return _builder_ret;
        }
    }
if (string_equals(keep, "") == 0) {
        {
#line 173 "cmd/zsync.ae"
fs_write_binary(keep, body, n);
        }
    }
#line 175 "cmd/zsync.ae"
    _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(body), _heap_body), n, ""};
    /* deferred */ if (_heap_me) { aether_heap_str_free(me); me = NULL; _heap_me = 0; }
    /* deferred */ if (_heap_ims) { aether_heap_str_free(ims); ims = NULL; _heap_ims = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_me) { aether_heap_str_free(me); me = NULL; _heap_me = 0; }
    /* deferred */ if (_heap_ims) { aether_heap_str_free(ims); ims = NULL; _heap_ims = 0; }
}

#line 181 "cmd/zsync.ae"
const char* fetch_from_urls(void* st, void* ctl, const char* source, const char* auth, int quiet) {
    int _heap_last_err = 0; (void)_heap_last_err;
    const char* last_err = NULL;
    int _heap_u = 0; (void)_heap_u;
    const char* u = NULL;
    int _heap_e = 0; (void)_heap_e;
    const char* e = NULL;
#line 182 "cmd/zsync.ae"
void* urls = control_ctl_urls(ctl);
if (urls == NULL) {
        {
#line 184 "cmd/zsync.ae"
            const char* _builder_ret = "no download URLs known";
            if (_heap_last_err) { aether_heap_str_free(last_err); last_err = NULL; _heap_last_err = 0; }
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_u) { aether_heap_str_free(u); u = NULL; _heap_u = 0; }
            return _builder_ret;
        }
    }
#line 186 "cmd/zsync.ae"
int n = list_size(urls);
if (n == 0) {
        {
#line 188 "cmd/zsync.ae"
            const char* _builder_ret = "no download URLs known";
            if (_heap_last_err) { aether_heap_str_free(last_err); last_err = NULL; _heap_last_err = 0; }
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_u) { aether_heap_str_free(u); u = NULL; _heap_u = 0; }
            return _builder_ret;
        }
    }
#line 190 "cmd/zsync.ae"
{ const char* _tmp_old = last_err; last_err = "no download URLs known"; if (_heap_last_err) aether_heap_str_free(_tmp_old); _heap_last_err = 0; }
#line 191 "cmd/zsync.ae"
int i = 0;
while (i < n) {
        {
if (download_status(st) >= 2) {
                {
#line 194 "cmd/zsync.ae"
                    const char* _builder_ret = "";
                    if (_heap_last_err) { aether_heap_str_free(last_err); last_err = NULL; _heap_last_err = 0; }
                    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
                    /* deferred */ if (_heap_u) { aether_heap_str_free(u); u = NULL; _heap_u = 0; }
                    return _builder_ret;
                }
            }
#line 196 "cmd/zsync.ae"
{ const char* _tmp_old = u; u = resolve(fileio_buf_as_string(list_get_raw(urls, i)), source); if (_heap_u) aether_heap_str_free(_tmp_old); _heap_u = 1; }
if (quiet == 0) {
                {
#line 198 "cmd/zsync.ae"
printf("downloading new blocks from %s", _aether_safe_str(u)); putchar('\n');
                }
            }
#line 200 "cmd/zsync.ae"
{ const char* _tmp_old = e; e = clientlib_fetch_remaining(st, u, auth); if (_heap_e) aether_heap_str_free(_tmp_old); _heap_e = 1; }
if (strcmp(_aether_safe_str(e), _aether_safe_str("")) == 0) {
                {
#line 202 "cmd/zsync.ae"
                    const char* _builder_ret = "";
                    if (_heap_last_err) { aether_heap_str_free(last_err); last_err = NULL; _heap_last_err = 0; }
                    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
                    /* deferred */ if (_heap_u) { aether_heap_str_free(u); u = NULL; _heap_u = 0; }
                    return _builder_ret;
                }
            }
#line 204 "cmd/zsync.ae"
{ const char* _tmp_old = last_err; last_err = aether_uniform_heap_str(e, 0); if (_heap_last_err) aether_heap_str_free(_tmp_old); _heap_last_err = 1; }
if (quiet == 0) {
                {
#line 206 "cmd/zsync.ae"
printf("failed from %s: %s", _aether_safe_str(u), _aether_safe_str(e)); putchar('\n');
                }
            }
#line 208 "cmd/zsync.ae"
i = (i + 1);
        }
    }
if (download_status(st) >= 2) {
        {
#line 211 "cmd/zsync.ae"
            const char* _builder_ret = "";
            if (_heap_last_err) { aether_heap_str_free(last_err); last_err = NULL; _heap_last_err = 0; }
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_u) { aether_heap_str_free(u); u = NULL; _heap_u = 0; }
            return _builder_ret;
        }
    }
#line 213 "cmd/zsync.ae"
    const char* _builder_ret = last_err;
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_u) { aether_heap_str_free(u); u = NULL; _heap_u = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_u) { aether_heap_str_free(u); u = NULL; _heap_u = 0; }
}

#line 216 "cmd/zsync.ae"
void feed_seed_if_exists(void* st, const char* path, int quiet) {
    int _heap_data = 0; (void)_heap_data;
    const char* data = NULL;
    int _heap_e = 0; (void)_heap_e;
    const char* e = NULL;
if (fs_exists(path) == 0) {
        {
#line 218 "cmd/zsync.ae"
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_data) { aether_heap_str_free(data); data = NULL; _heap_data = 0; }
            return;
        }
    }
#line 220 "cmd/zsync.ae"
    _tuple_string_int_string _tup13 = fs_read_binary(path);
    { const char* _tmp_old = data; data = _tup13._0; if (_heap_data) aether_heap_str_free(_tmp_old); _heap_data = 1; }
    int n = _tup13._1;
    { const char* _tmp_old = e; e = _tup13._2; if (_heap_e) aether_heap_str_free(_tmp_old); _heap_e = 0; }
if (strcmp(_aether_safe_str(e), _aether_safe_str("")) != 0) {
        {
#line 222 "cmd/zsync.ae"
            /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
            /* deferred */ if (_heap_data) { aether_heap_str_free(data); data = NULL; _heap_data = 0; }
            return;
        }
    }
if (quiet == 0) {
        {
#line 225 "cmd/zsync.ae"
printf("reading seed file %s", _aether_safe_str(path)); putchar('\n');
        }
    }
#line 227 "cmd/zsync.ae"
download_submit_source(st, data, n);
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_data) { aether_heap_str_free(data); data = NULL; _heap_data = 0; }
}

#line 232 "cmd/zsync.ae"
const char* creds_of(const char* v) {
#line 233 "cmd/zsync.ae"
int eq = string_index_of(v, "=");
if (eq < 0) {
        {
#line 235 "cmd/zsync.ae"
            return aether_uniform_heap_str((const char*)(v), 0);
        }
    }
#line 237 "cmd/zsync.ae"
    return aether_uniform_heap_str((const char*)(string_substring(v, (eq + 1), string_length(v))), 1);
}

#line 242 "cmd/zsync.ae"
const char* resolve(const char* u, const char* source) {
if (is_abs(u) == 1) {
        {
#line 244 "cmd/zsync.ae"
            return aether_uniform_heap_str((const char*)(u), 0);
        }
    }
#line 246 "cmd/zsync.ae"
int last = last_slash(source);
if (last < 0) {
        {
#line 248 "cmd/zsync.ae"
            return aether_uniform_heap_str((const char*)(u), 0);
        }
    }
#line 250 "cmd/zsync.ae"
    return aether_uniform_heap_str((const char*)(({ const char* _ad_15 = (const char*)(string_substring(source, 0, (last + 1))); const char* _ad_r = string_concat(_ad_15, u); aether_heap_str_free(_ad_15); _ad_r; })), 1);
}

#line 253 "cmd/zsync.ae"
int is_abs(const char* u) {
if (string_starts_with(u, "http://") == 1) {
        {
#line 255 "cmd/zsync.ae"
            return 1;
        }
    }
if (string_starts_with(u, "https://") == 1) {
        {
#line 258 "cmd/zsync.ae"
            return 1;
        }
    }
#line 260 "cmd/zsync.ae"
    return 0;
}

#line 263 "cmd/zsync.ae"
int last_slash(const char* s) {
#line 264 "cmd/zsync.ae"
int n = string_length(s);
#line 265 "cmd/zsync.ae"
int last = -1;
#line 266 "cmd/zsync.ae"
int i = 0;
while (i < n) {
        {
if ((string_char_at(s, i) & 0xff) == 47) {
                {
#line 269 "cmd/zsync.ae"
last = i;
                }
            }
#line 271 "cmd/zsync.ae"
i = (i + 1);
        }
    }
#line 273 "cmd/zsync.ae"
    return last;
}

#line 277 "cmd/zsync.ae"
const char* str_owned(const char* s) {
#line 278 "cmd/zsync.ae"
    return aether_uniform_heap_str((const char*)(string_concat(s, "")), 1);
}

