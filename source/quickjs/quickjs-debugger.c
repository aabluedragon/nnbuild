/* Private, length-prefixed JSON debugger protocol used by VibeBuild32's
 * built-in browser DevTools client.  It deliberately has no IDE dependency. */
#include "quickjs-debugger.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_all(JSDebuggerInfo *d, char *p, size_t n)
{
    while (n) { size_t got = d->transport_read(d->transport_udata, p, n); if (!got) return 0; p += got; n -= got; }
    return 1;
}
static int write_all(JSDebuggerInfo *d, const char *p, size_t n)
{
    while (n) { size_t sent = d->transport_write(d->transport_udata, p, n); if (!sent) return 0; p += sent; n -= sent; }
    return 1;
}
static int send_value(JSDebuggerInfo *d, JSValue value)
{
    JSValue json = JS_JSONStringify(d->ctx, value, JS_UNDEFINED, JS_UNDEFINED);
    size_t n = 0; const char *text = JS_ToCStringLen(d->ctx, &n, json);
    char header[10]; snprintf(header, sizeof(header), "%08x\n", (unsigned)(n + 1));
    int ok = text && write_all(d, header, 9) && write_all(d, text, n) && write_all(d, "\n", 1);
    if (text) JS_FreeCString(d->ctx, text);
    JS_FreeValue(d->ctx, json); JS_FreeValue(d->ctx, value);
    return ok;
}
static void response(JSDebuggerInfo *d, JSValue request, JSValue body)
{
    JSValue envelope = JS_NewObject(d->ctx);
    JS_SetPropertyStr(d->ctx, envelope, "type", JS_NewString(d->ctx, "response"));
    JS_SetPropertyStr(d->ctx, envelope, "request_seq", JS_GetPropertyStr(d->ctx, request, "request_seq"));
    JS_SetPropertyStr(d->ctx, envelope, "body", body);
    send_value(d, envelope);
}
static void stopped(JSDebuggerInfo *d, const char *reason, const uint8_t *pc)
{
    JSValue event = JS_NewObject(d->ctx), data = JS_NewObject(d->ctx);
    JS_SetPropertyStr(d->ctx, data, "reason", JS_NewString(d->ctx, reason));
    JSDebuggerLocation at = js_debugger_current_location(d->ctx, pc);
    if (at.filename) JS_SetPropertyStr(d->ctx, data, "filename", JS_AtomToString(d->ctx, at.filename));
    JS_SetPropertyStr(d->ctx, data, "line", JS_NewInt32(d->ctx, at.line));
    JS_SetPropertyStr(d->ctx, data, "column", JS_NewInt32(d->ctx, at.column));
    JS_SetPropertyStr(d->ctx, event, "type", JS_NewString(d->ctx, "event"));
    JS_SetPropertyStr(d->ctx, event, "event", data);
    send_value(d, event);
}
static JSValue scopes(JSContext *ctx, int frame)
{
    JSValue list = JS_NewArray(ctx);
    const char *names[] = { "Local", "Closure", "Global" };
    for (int i = 0; i < 3; ++i) { JSValue item = JS_NewObject(ctx); JS_SetPropertyStr(ctx, item, "name", JS_NewString(ctx, names[i])); JS_SetPropertyStr(ctx, item, "reference", JS_NewInt32(ctx, (frame << 2) + i + 1)); JS_SetPropertyUint32(ctx, list, i, item); }
    return list;
}
static JSValue variables(JSContext *ctx, uint32_t reference)
{
    int frame = (int)(reference >> 2), kind = (int)((reference & 3) - 1);
    /* Return the raw scope object.  JSON serialization safely creates the
       browser-facing property view without exposing QuickJS internals here. */
    return kind == 0 ? js_debugger_local_variables(ctx, frame) : kind == 1 ? js_debugger_closure_variables(ctx, frame) : JS_GetGlobalObject(ctx);
}
static int get_frame(JSContext *ctx, JSValue request)
{
    JSValue args = JS_GetPropertyStr(ctx, request, "args"), value = JS_GetPropertyStr(ctx, args, "frame"); int32_t frame = 0; JS_ToInt32(ctx, &frame, value); JS_FreeValue(ctx, value); JS_FreeValue(ctx, args); return frame;
}
static void handle_request(JSDebuggerInfo *d, JSValue request, const uint8_t *pc)
{
    JSContext *ctx = d->ctx; JSValue commandValue = JS_GetPropertyStr(ctx, request, "command"); const char *command = JS_ToCString(ctx, commandValue);
    if (!command) goto done;
    if (!strcmp(command, "continue") || !strcmp(command, "next") || !strcmp(command, "stepIn") || !strcmp(command, "stepOut"))
    {
        d->stepping = !strcmp(command, "continue") ? JS_DEBUGGER_STEP_CONTINUE : !strcmp(command, "next") ? JS_DEBUGGER_STEP : !strcmp(command, "stepIn") ? JS_DEBUGGER_STEP_IN : JS_DEBUGGER_STEP_OUT;
        d->step_over = js_debugger_current_location(ctx, pc);
        /* Continue must leave this breakpoint rather than immediately pause
           again at another bytecode mapped to the same source line. */
        if (!strcmp(command, "continue")) { d->ignored_breakpoint = d->step_over; d->ignored_breakpoint_active = 1; }
        d->step_depth = (int)js_debugger_stack_depth(ctx); d->is_paused = 0; response(d, request, JS_UNDEFINED);
    }
    else if (!strcmp(command, "stackTrace")) response(d, request, js_debugger_build_backtrace(ctx, pc));
    else if (!strcmp(command, "scopes")) response(d, request, scopes(ctx, get_frame(ctx, request)));
    else if (!strcmp(command, "variables")) { JSValue args = JS_GetPropertyStr(ctx, request, "args"), ref = JS_GetPropertyStr(ctx, args, "variablesReference"); uint32_t n = 0; JS_ToUint32(ctx, &n, ref); JS_FreeValue(ctx, ref); JS_FreeValue(ctx, args); response(d, request, variables(ctx, n)); }
    else if (!strcmp(command, "evaluate")) { JSValue args = JS_GetPropertyStr(ctx, request, "args"), expression = JS_GetPropertyStr(ctx, args, "expression"); JSValue value = js_debugger_evaluate(ctx, get_frame(ctx, request), expression); JS_FreeValue(ctx, expression); JS_FreeValue(ctx, args); JSValue result = JS_NewObject(ctx); JS_SetPropertyStr(ctx, result, "result", JS_ToString(ctx, value)); JS_FreeValue(ctx, value); response(d, request, result); }
    else response(d, request, JS_UNDEFINED);
done:
    if (command) JS_FreeCString(ctx, command); JS_FreeValue(ctx, commandValue);
}
static int process_one(JSDebuggerInfo *d, const uint8_t *pc)
{
    char header[10]; if (!read_all(d, header, 9)) return 0; header[8] = 0;
    unsigned n = 0; if (sscanf(header, "%x", &n) != 1 || !n) return 0;
    char *buffer = (char *)realloc(d->message_buffer, n + 1); if (!buffer) return 0; d->message_buffer = buffer; d->message_buffer_length = (int)n;
    if (!read_all(d, buffer, n)) return 0; buffer[n] = 0;
    JSValue message = JS_ParseJSON(d->ctx, buffer, n, "<debugger>");
    JSValue typeValue = JS_GetPropertyStr(d->ctx, message, "type"); const char *type = JS_ToCString(d->ctx, typeValue);
    if (type && !strcmp(type, "request")) { JSValue request = JS_GetPropertyStr(d->ctx, message, "request"); handle_request(d, request, pc); JS_FreeValue(d->ctx, request); }
    else if (type && !strcmp(type, "configurationDone")) d->configuration_done = 1;
    else if (type && !strcmp(type, "breakpoints"))
    {
        JSValue set = JS_GetPropertyStr(d->ctx, message, "breakpoints");
        JSValue path = JS_GetPropertyStr(d->ctx, set, "path");
        JSValue lines = JS_GetPropertyStr(d->ctx, set, "breakpoints");
        const char *name = JS_ToCString(d->ctx, path);
        if (name)
        {
            JSDebuggerBreakpointFile *file = NULL;
            for (int i = 0; i < d->breakpoint_file_count; ++i)
                if (!strcmp(d->breakpoint_files[i].path, name)) { file = &d->breakpoint_files[i]; break; }
            if (!file && d->breakpoint_file_count < (int)(sizeof(d->breakpoint_files) / sizeof(d->breakpoint_files[0])))
            {
                file = &d->breakpoint_files[d->breakpoint_file_count++];
                snprintf(file->path, sizeof(file->path), "%s", name);
            }
            if (file)
            {
                file->line_count = 0;
                JSValue length = JS_GetPropertyStr(d->ctx, lines, "length");
                uint32_t count = 0; JS_ToUint32(d->ctx, &count, length); JS_FreeValue(d->ctx, length);
                for (uint32_t i = 0; i < count && file->line_count < (int)(sizeof(file->lines) / sizeof(file->lines[0])); ++i)
                {
                    JSValue item = JS_GetPropertyUint32(d->ctx, lines, i);
                    JSValue line = JS_GetPropertyStr(d->ctx, item, "line");
                    int32_t value = 0; JS_ToInt32(d->ctx, &value, line);
                    if (value > 0) file->lines[file->line_count++] = value;
                    JS_FreeValue(d->ctx, line); JS_FreeValue(d->ctx, item);
                }
                ++d->breakpoints_dirty_counter;
            }
            JS_FreeCString(d->ctx, name);
        }
        JS_FreeValue(d->ctx, lines); JS_FreeValue(d->ctx, path); JS_FreeValue(d->ctx, set);
    }
    if (type) JS_FreeCString(d->ctx, type); JS_FreeValue(d->ctx, typeValue); JS_FreeValue(d->ctx, message); return 1;
}

JSValue js_debugger_file_breakpoints(JSContext *ctx, const char *path)
{
    (void)ctx; (void)path;
    return JS_UNDEFINED;
}
void js_debugger_new_context(JSContext *ctx) { (void)ctx; }
void js_debugger_free_context(JSContext *ctx) { (void)ctx; }
void js_debugger_exception(JSContext *ctx) { (void)ctx; }
void js_debugger_free(JSRuntime *rt, JSDebuggerInfo *d) { if (d->transport_close) d->transport_close(rt, d->transport_udata); if (!JS_IsUndefined(d->breakpoints)) JS_FreeValueRT(rt, d->breakpoints); free(d->message_buffer); memset(d, 0, sizeof(*d)); d->breakpoints = JS_UNDEFINED; }
void js_debugger_attach(JSContext *ctx, size_t (*read)(void *, char *, size_t), size_t (*write)(void *, const char *, size_t), size_t (*peek)(void *), void (*close)(JSRuntime *, void *), void *udata) { JSDebuggerInfo *d = js_debugger_info(JS_GetRuntime(ctx)); d->ctx = ctx; d->transport_read = read; d->transport_write = write; d->transport_peek = peek; d->transport_close = close; d->transport_udata = udata; d->configuration_done = 0; d->breakpoints = JS_NewObject(ctx); d->breakpoints_dirty_counter = 1; }
void js_debugger_cooperate(JSContext *ctx) { JSDebuggerInfo *d = js_debugger_info(JS_GetRuntime(ctx)); if (d->transport_close && d->transport_peek(d->transport_udata)) process_one(d, NULL); }
int js_debugger_configuration_done(JSContext *ctx) { return js_debugger_info(JS_GetRuntime(ctx))->configuration_done; }
static int ignore_current_breakpoint(JSDebuggerInfo *d, JSDebuggerLocation at)
{
    if (!d->ignored_breakpoint_active) return 0;
    if (at.filename == d->ignored_breakpoint.filename && at.line == d->ignored_breakpoint.line) return 1;
    d->ignored_breakpoint_active = 0;
    return 0;
}
void js_debugger_check(JSContext *ctx, const uint8_t *pc)
{
    JSDebuggerInfo *d = js_debugger_info(JS_GetRuntime(ctx)); if (d->is_debugging) return; d->is_debugging = 1; d->ctx = ctx;
    while (!d->is_paused && d->transport_peek && d->transport_peek(d->transport_udata) && process_one(d, pc)) {}
    if (!d->is_paused && !d->stepping && d->breakpoint_file_count <= 0) { d->is_debugging = 0; return; }
    JSDebuggerLocation at = js_debugger_current_location(ctx, pc); int depth = (int)js_debugger_stack_depth(ctx);
    int hit = at.filename && !ignore_current_breakpoint(d, at) && js_debugger_check_breakpoint(ctx, d->breakpoints_dirty_counter, pc);
    int const sameLocation = at.filename == d->step_over.filename && at.line == d->step_over.line && at.column == d->step_over.column;
    if (d->stepping)
    {
        /* Continue/step always leave the current source location first.  A
           source line commonly contains several bytecodes, so without this
           guard Continue would immediately stop on its own breakpoint. */
        if (sameLocation) hit = 0;
        else if (d->stepping == JS_DEBUGGER_STEP_CONTINUE) d->stepping = 0;
        else if (d->stepping == JS_DEBUGGER_STEP_IN ||
                 (d->stepping == JS_DEBUGGER_STEP && depth <= d->step_depth) ||
                 (d->stepping == JS_DEBUGGER_STEP_OUT && depth < d->step_depth)) hit = 1;
    }
    if (hit) { d->stepping = 0; d->is_paused = 1; stopped(d, "breakpoint", pc); }
    while (d->is_paused && process_one(d, pc)) {}
    if (!d->is_paused && d->transport_peek && d->transport_peek(d->transport_udata))
    {
        process_one(d, pc);
        /* A breakpoint update can arrive immediately before a handler's
           first opcode. Re-evaluate this exact source location so a short
           function cannot run past a newly-installed source breakpoint. */
        if (!d->is_paused && at.filename && !ignore_current_breakpoint(d, at) && js_debugger_check_breakpoint(ctx, d->breakpoints_dirty_counter, pc))
        {
            d->is_paused = 1; stopped(d, "breakpoint", pc);
            while (d->is_paused && process_one(d, pc)) {}
        }
    }
    d->is_debugging = 0;
}
JSValue js_debugger_evaluate(JSContext *ctx, int frame, JSValue expression)
{
    size_t length = 0; const char *text = JS_ToCStringLen(ctx, &length, expression);
    JSValue result = JS_EXCEPTION;
    /* VS Code hovers normally ask for a single identifier. Resolve that from
       the paused frame first, matching the Variables pane rather than falling
       through to an unrelated global of the same name. */
    int identifier = text && length > 0 && (isalpha((unsigned char)text[0]) || text[0] == '_' || text[0] == '$');
    for (size_t i = 1; identifier && i < length; ++i)
        if (!(isalnum((unsigned char)text[i]) || text[i] == '_' || text[i] == '$')) identifier = 0;
    if (identifier)
    {
        JSValue locals = js_debugger_local_variables(ctx, frame);
        result = JS_GetPropertyStr(ctx, locals, text);
        JS_FreeValue(ctx, locals);
        if (JS_IsUndefined(result))
        {
            JS_FreeValue(ctx, result);
            JSValue closure = js_debugger_closure_variables(ctx, frame);
            result = JS_GetPropertyStr(ctx, closure, text);
            JS_FreeValue(ctx, closure);
        }
        if (!JS_IsUndefined(result)) { if (text) JS_FreeCString(ctx, text); return result; }
        JS_FreeValue(ctx, result);
    }
    result = text ? JS_Eval(ctx, text, length, "<devtools>", JS_EVAL_TYPE_GLOBAL) : JS_EXCEPTION;
    if (text) JS_FreeCString(ctx, text);
    return result;
}
