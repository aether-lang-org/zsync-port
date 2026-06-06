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

typedef struct Ctx Ctx;
typedef struct LocalTime LocalTime;
typedef struct Ctx {
    const char* base;
    int _heap_base;
} Ctx;
static inline void Ctx_destroy(Ctx* s) {
    if (!s) return;
    if (s->_heap_base) { aether_heap_str_free(s->base); s->base = (const char*)0; s->_heap_base = 0; }
}

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

// Forward declarations
void* ctx_as_ptr(Ctx*);
void serve_handler(void*, void*, void*);
int parse_num(const char*);
static const char* http_server_start(void*);

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

// Extern C function: http_get_raw
void* http_get_raw(const char*);

// Extern C function: http_get_with_timeout_raw
void* http_get_with_timeout_raw(const char*, int);

// Extern C function: http_get_with_timeout_ns_raw
void* http_get_with_timeout_ns_raw(const char*, int64_t);

// Extern C function: http_post_raw
void* http_post_raw(const char*, const char*, const char*);

// Extern C function: http_put_raw
void* http_put_raw(const char*, const char*, const char*);

// Extern C function: http_delete_raw
void* http_delete_raw(const char*);

// Extern C function: http_response_free
void http_response_free(void*);

// Extern C function: http_response_status_code
int http_response_status_code(void*);

// Extern C function: http_response_body_str
const char* http_response_body_str(void*);

// Extern C function: http_response_headers_str
const char* http_response_headers_str(void*);

// Extern C function: http_response_status
int http_response_status(void*);

// Extern C function: http_response_body
const char* http_response_body(void*);

// Extern C function: http_response_headers
const char* http_response_headers(void*);

// Extern C function: http_response_error
const char* http_response_error(void*);

// Extern C function: http_response_ok
int http_response_ok(void*);

// Extern C function: string_concat
const char* string_concat(const char*, const char*);

// Extern C function: http_server_create
void* http_server_create(int);

// Extern C function: http_server_bind_raw
int http_server_bind_raw(void*, const char*, int);

// Extern C function: http_server_port
int http_server_port(void*);

// Extern C function: http_server_set_host
void http_server_set_host(void*, const char*);

// Extern C function: http_server_start_raw
int http_server_start_raw(void*);

// Extern C function: http_server_start_background_raw
int http_server_start_background_raw(void*);

// Extern C function: http_server_stop
void http_server_stop(void*);

// Extern C function: http_server_free
void http_server_free(void*);

// Extern C function: http_server_set_tls_raw
const char* http_server_set_tls_raw(void*, const char*, const char*);

// Extern C function: http_server_set_h2_raw
const char* http_server_set_h2_raw(void*, int);

// Extern C function: http_server_set_h2_concurrent_dispatch_raw
const char* http_server_set_h2_concurrent_dispatch_raw(void*, int);

// Extern C function: http_server_set_keepalive_raw
const char* http_server_set_keepalive_raw(void*, int, int, int64_t);

// Extern C function: http_server_shutdown_graceful_raw
const char* http_server_shutdown_graceful_raw(void*, int64_t);

// Extern C function: http_server_set_on_start
void http_server_set_on_start(void*, void*, void*);

// Extern C function: http_server_set_on_stop
void http_server_set_on_stop(void*, void*, void*);

// Extern C function: http_server_set_health_probes_raw
const char* http_server_set_health_probes_raw(void*, const char*, const char*, void*, void*);

// Extern C function: http_server_set_access_log_raw
const char* http_server_set_access_log_raw(void*, const char*, const char*);

// Extern C function: http_server_set_metrics_raw
const char* http_server_set_metrics_raw(void*, const char*);

// Extern C function: http_server_sse
void http_server_sse(void*, const char*, void*, void*);

// Extern C function: http_sse_send_event
int http_sse_send_event(void*, const char*, const char*);

// Extern C function: http_sse_send_event_id
int http_sse_send_event_id(void*, const char*, const char*, const char*);

// Extern C function: http_sse_close
void http_sse_close(void*);

// Extern C function: http_server_websocket
void http_server_websocket(void*, const char*, void*, void*);

// Extern C function: http_ws_send_text
int http_ws_send_text(void*, const char*);

// Extern C function: http_ws_send_binary
int http_ws_send_binary(void*, void*, int);

// Extern C function: http_ws_recv
int http_ws_recv(void*);

// Extern C function: http_ws_message_data
const char* http_ws_message_data(void*);

// Extern C function: http_ws_message_length
int http_ws_message_length(void*);

// Extern C function: http_ws_close
void http_ws_close(void*, int, const char*);

// Extern C function: http_server_drain_connection
void http_server_drain_connection(void*, int);

// Extern C function: http_server_add_route
void http_server_add_route(void*, const char*, const char*, void*, void*);

// Extern C function: http_server_get
void http_server_get(void*, const char*, void*, void*);

// Extern C function: http_server_post
void http_server_post(void*, const char*, void*, void*);

// Extern C function: http_server_put
void http_server_put(void*, const char*, void*, void*);

// Extern C function: http_server_delete
void http_server_delete(void*, const char*, void*, void*);

// Extern C function: http_server_use_middleware
void http_server_use_middleware(void*, void*, void*);

// Extern C function: http_get_header
const char* http_get_header(void*, const char*);

// Extern C function: http_get_query_param
const char* http_get_query_param(void*, const char*);

// Extern C function: http_get_path_param
const char* http_get_path_param(void*, const char*);

// Extern C function: http_request_free
void http_request_free(void*);

// Extern C function: http_response_create
void* http_response_create(void);

// Extern C function: http_response_set_status
void http_response_set_status(void*, int);

// Extern C function: http_response_set_header
void http_response_set_header(void*, const char*, const char*);

// Extern C function: http_response_add_header
void http_response_add_header(void*, const char*, const char*);

// Extern C function: http_response_clear_headers
void http_response_clear_headers(void*);

// Extern C function: http_response_set_body
void http_response_set_body(void*, const char*);

// Extern C function: http_response_set_body_n
void http_response_set_body_n(void*, const char*, int);

// Extern C function: http_response_json
void http_response_json(void*, const char*);

// Extern C function: http_server_response_free
void http_server_response_free(void*);

// Extern C function: http_server_set_actor_handler
void http_server_set_actor_handler(void*, void*, void*, void*, void*);

// Extern C function: http_request_method
const char* http_request_method(void*);

// Extern C function: http_request_path
const char* http_request_path(void*);

// Extern C function: http_request_body
const char* http_request_body(void*);

// Extern C function: http_request_body_length
int http_request_body_length(void*);

// Extern C function: http_request_body_read_raw
int http_request_body_read_raw(void*, int, int);

// Extern C function: http_get_request_body_read
const char* http_get_request_body_read(void);

// Extern C function: http_get_request_body_read_length
int http_get_request_body_read_length(void);

// Extern C function: http_release_request_body_read
void http_release_request_body_read(void);

// Extern C function: string_new_with_length
void* string_new_with_length(const char*, int);

// Extern C function: http_request_query
const char* http_request_query(void*);

// Extern C function: http_request_header_count
int http_request_header_count(void*);

// Extern C function: http_request_header_name
const char* http_request_header_name(void*, int);

// Extern C function: http_request_header_value
const char* http_request_header_value(void*, int);

// Extern C function: http_serve_static
void http_serve_static(void*, void*, const char*);

// Extern C function: http_serve_file
void http_serve_file(void*, const char*);

// Extern C function: http_mime_type
const char* http_mime_type(const char*);

// Extern C function: ae_io_await
int ae_io_await(int);

// Extern C function: ae_io_cancel
void ae_io_cancel(int);

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

// Extern C function: malloc (libc-provided, declaration skipped)

// Import: std.string
// Import: std.http
// Import: std.os
#line 19 "cmd/fileserver.ae"
void* ctx_as_ptr(Ctx* c) {
#line 20 "cmd/fileserver.ae"
    return c;
}

#line 24 "cmd/fileserver.ae"
void serve_handler(void* req, void* res, void* ud) {
#line 25 "cmd/fileserver.ae"
Ctx* ctx = ((Ctx*)(ud));
#line 28 "cmd/fileserver.ae"
http_serve_static(req, res, aether_string_data(ctx->base));
}

#line 31 "cmd/fileserver.ae"
int parse_num(const char* s) {
#line 32 "cmd/fileserver.ae"
int ok = string_try_long(s);
if (ok == 0) {
        {
#line 33 "cmd/fileserver.ae"
            return 0;
        }
    }
#line 34 "cmd/fileserver.ae"
    return string_get_long(s);
}

#line 534 "/home/paul/scm/aether/build/../std/http/module.ae"
static const char* http_server_start(void* server) {
#line 535 "/home/paul/scm/aether/build/../std/http/module.ae"
int rc = http_server_start_raw(server);
if (rc < 0) {
        {
#line 537 "/home/paul/scm/aether/build/../std/http/module.ae"
            return "server start failed";
        }
    }
#line 539 "/home/paul/scm/aether/build/../std/http/module.ae"
    return "";
}

int main(int argc, char** argv) {
    #ifdef _WIN32
    SetConsoleOutputCP(65001);  // CP_UTF8
    SetConsoleCP(65001);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    #endif
    aether_args_init(argc, argv);
    
    int _heap_base = 0; (void)_heap_base;
    const char* base = NULL;
    {
#line 38 "cmd/fileserver.ae"
int argc = aether_args_count();
if (argc < 3) {
            {
#line 40 "cmd/fileserver.ae"
puts("usage: fileserver <port> <base-dir>");
#line 41 "cmd/fileserver.ae"
exit(2);
            }
        }
#line 43 "cmd/fileserver.ae"
int port = parse_num(aether_args_get(1));
#line 44 "cmd/fileserver.ae"
base = aether_args_get(2);
#line 46 "cmd/fileserver.ae"
void* craw = malloc(16);
#line 47 "cmd/fileserver.ae"
Ctx* ctx = ((Ctx*)(craw));
#line 48 "cmd/fileserver.ae"
(ctx->base = base);
#line 50 "cmd/fileserver.ae"
void* server = http_server_create(port);
#line 51 "cmd/fileserver.ae"
http_server_set_host(server, aether_string_data("127.0.0.1"));
#line 52 "cmd/fileserver.ae"
http_server_get(server, aether_string_data("/*"), serve_handler, ctx_as_ptr(ctx));
#line 53 "cmd/fileserver.ae"
printf("fileserver on 127.0.0.1:%d serving %s", port, _aether_safe_str(base)); putchar('\n');
#line 54 "cmd/fileserver.ae"
http_server_start(server);
    }
    return 0;
}
