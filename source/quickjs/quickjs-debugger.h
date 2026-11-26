/* Internal source debugger support for the embedded QuickJS runtime.
 * This is deliberately private to the engine: public game code talks to it
 * through NNBuild's DevTools bridge, never through QuickJS internals. */
#ifndef QUICKJS_DEBUGGER_H
#define QUICKJS_DEBUGGER_H

#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JSDebuggerFunctionInfo {
    uint8_t *breakpoints;
    uint32_t dirty;
    int last_line_num;
} JSDebuggerFunctionInfo;

typedef struct JSDebuggerLocation {
    JSAtom filename;
    int line;
    int column;
} JSDebuggerLocation;

typedef struct JSDebuggerBreakpointFile {
    char path[1024];
    int lines[128];
    int line_count;
} JSDebuggerBreakpointFile;

#define JS_DEBUGGER_STEP       1
#define JS_DEBUGGER_STEP_IN    2
#define JS_DEBUGGER_STEP_OUT   3
#define JS_DEBUGGER_STEP_CONTINUE 4

typedef struct JSDebuggerInfo {
    JSContext *ctx;
    JSContext *debugging_ctx;
    int is_debugging, is_paused, stepping, step_depth;
    int configuration_done;
    uint32_t breakpoints_dirty_counter;
    JSDebuggerLocation step_over;
    JSDebuggerLocation ignored_breakpoint;
    int ignored_breakpoint_active;
    JSValue breakpoints;
    JSDebuggerBreakpointFile breakpoint_files[64];
    int breakpoint_file_count;
    int exception_breakpoint;
    /* The host owns this synchronous, length-prefixed transport. */
    size_t (*transport_read)(void *, char *, size_t);
    size_t (*transport_write)(void *, const char *, size_t);
    size_t (*transport_peek)(void *);
    void (*transport_close)(JSRuntime *, void *);
    void *transport_udata;
    char *message_buffer;
    int message_buffer_length;
} JSDebuggerInfo;

void js_debugger_new_context(JSContext *ctx);
void js_debugger_free_context(JSContext *ctx);
void js_debugger_check(JSContext *ctx, const uint8_t *pc);
void js_debugger_exception(JSContext *ctx);
void js_debugger_free(JSRuntime *rt, JSDebuggerInfo *info);
void js_debugger_attach(JSContext *ctx, size_t (*read)(void *, char *, size_t),
                        size_t (*write)(void *, const char *, size_t),
                        size_t (*peek)(void *),
                        void (*close)(JSRuntime *, void *), void *udata);
void js_debugger_cooperate(JSContext *ctx);
int js_debugger_configuration_done(JSContext *ctx);
JSDebuggerInfo *js_debugger_info(JSRuntime *rt);
JSValue js_debugger_file_breakpoints(JSContext *ctx, const char *path);
int js_debugger_check_breakpoint(JSContext *ctx, uint32_t dirty, const uint8_t *pc);
JSDebuggerLocation js_debugger_current_location(JSContext *ctx, const uint8_t *pc);
uint32_t js_debugger_stack_depth(JSContext *ctx);
JSValue js_debugger_build_backtrace(JSContext *ctx, const uint8_t *pc);
JSValue js_debugger_local_variables(JSContext *ctx, int frame);
JSValue js_debugger_closure_variables(JSContext *ctx, int frame);
JSValue js_debugger_evaluate(JSContext *ctx, int frame, JSValue expression);

#ifdef __cplusplus
}
#endif
#endif
