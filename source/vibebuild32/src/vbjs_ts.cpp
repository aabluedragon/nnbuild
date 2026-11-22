// VibeBuild32 — minimal TypeScript -> JavaScript transpiler (type stripper).
//
// Not a full TS compiler: it removes type syntax so .ts mod scripts run directly
// in QuickJS. Handles the constructs typical mod code (and our generated example)
// uses: type annotations, interfaces, type aliases, `import/export type`, `as`
// casts, non-null `!`, access modifiers, optional `?:`, generic type args, and
// numeric/string enums. Exotic TS (conditional/mapped types, decorators, JSX,
// namespaces) is out of scope. It preserves line structure so error line numbers
// stay meaningful.
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

namespace {

struct SB { char *p; size_t n, cap; };
static void sb_init(SB *b) { b->cap = 4096; b->p = (char *)malloc(b->cap); b->n = 0; }
static void sb_ensure(SB *b, size_t extra) { if (b->n + extra + 1 > b->cap) { while (b->n + extra + 1 > b->cap) b->cap *= 2; b->p = (char *)realloc(b->p, b->cap); } }
static void sb_putc(SB *b, char c) { sb_ensure(b, 1); b->p[b->n++] = c; }
static void sb_put(SB *b, const char *s, size_t k) { sb_ensure(b, k); memcpy(b->p + b->n, s, k); b->n += k; }

static int is_id_start(int c) { return isalpha(c) || c == '_' || c == '$'; }
static int is_id(int c)       { return isalnum(c) || c == '_' || c == '$'; }

struct TS {
    const char *s;
    size_t i, len;
    SB out;

    // bracket stack: track whether a '{' is an object literal, and pending ternaries
    struct Frame { char br; int isObject; int ternary; };
    Frame stk[512];
    int sp = 0;

    // classification of the previous emitted significant token, to decide '{' kind,
    // regex-vs-divide, and whether '!'/'<' are postfix (type) operators.
    // 0 = none/operator (expression position), 1 = value (identifier/')'/']'/literal)
    int prevValue = 0;

    void putc_(char c)             { sb_putc(&out, c); }
    void puts_(const char *p, size_t k) { sb_put(&out, p, k); }

    char cur()  { return i < len ? s[i] : 0; }
    char at(size_t k) { return i + k < len ? s[i + k] : 0; }

    // copy a quoted string / template literal verbatim
    void copyString(char q)
    {
        putc_(s[i++]);                       // opening quote
        while (i < len)
        {
            char c = s[i];
            if (c == '\\') { putc_(c); if (i + 1 < len) putc_(s[i + 1]); i += 2; continue; }
            if (q == '`' && c == '$' && at(1) == '{')
            {
                puts_("${", 2); i += 2;
                copyBalancedTemplateExpr();
                continue;
            }
            putc_(c); i++;
            if (c == q) break;
        }
        prevValue = 1;
    }

    // inside `${ ... }` — run the normal transform until the matching '}'
    void copyBalancedTemplateExpr()
    {
        int depth = 1;
        while (i < len && depth > 0)
        {
            char c = s[i];
            if (c == '}') { depth--; if (depth == 0) { putc_('}'); i++; break; } }
            else if (c == '{') depth++;
            step();      // recursive normal processing (handles nested strings etc.)
        }
    }

    void copyLineComment()  { while (i < len && s[i] != '\n') putc_(s[i++]); }
    void copyBlockComment() { puts_("/*", 2); i += 2; while (i < len && !(s[i] == '*' && at(1) == '/')) putc_(s[i++]); if (i < len) { puts_("*/", 2); i += 2; } }

    void skipWs(int alsoNewlines = 1) { while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || (alsoNewlines && s[i] == '\n'))) i++; }

    int matchWord(const char *w)   // s[i..] starts with word w on an identifier boundary
    {
        size_t k = strlen(w);
        if (i + k > len || strncmp(s + i, w, k) != 0) return 0;
        if (i + k < len && is_id((unsigned char)s[i + k])) return 0;
        if (i > 0 && is_id((unsigned char)s[i - 1])) return 0;
        return 1;
    }

    // discard a balanced (...) [...] {...} <...> starting at s[i] (open char)
    void skipBalanced(char open, char close)
    {
        int d = 0;
        while (i < len)
        {
            char c = s[i];
            if (c == '\'' || c == '"' || c == '`') { skipStringNoEmit(); continue; }
            if (c == open) d++;
            else if (c == close) { d--; i++; if (d == 0) return; else continue; }
            i++;
        }
    }
    void skipStringNoEmit()
    {
        char q = s[i++];
        while (i < len) { char c = s[i]; if (c == '\\') { i += 2; continue; } i++; if (c == q) break; }
    }

    // Discard a type expression starting at s[i] (right after a ':' / 'as' / '|' etc.)
    // Stops (without consuming) at a top-level ',', ';', ')', '}', ']', '=' (not '=>'),
    // or a '{' that begins a block (not an object type).
    void skipType()
    {
        int expectAtom = 1;
        int prevWasParenGroup = 0;   // last atom was `(...)` -> a following `=>` is a fn-type arrow
        for (;;)
        {
            // whitespace/comments inside types are dropped
            while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
            if (i >= len) return;
            if (s[i] == '/' && at(1) == '/') { while (i < len && s[i] != '\n') i++; continue; }
            if (s[i] == '/' && at(1) == '*') { i += 2; while (i < len && !(s[i] == '*' && at(1) == '/')) i++; if (i < len) i += 2; continue; }

            char c = s[i];
            if (expectAtom)
            {
                if (c == '{') { skipBalanced('{', '}'); expectAtom = 0; prevWasParenGroup = 0; continue; }
                if (c == '(') { skipBalanced('(', ')'); expectAtom = 0; prevWasParenGroup = 1; continue; }
                if (c == '[') { skipBalanced('[', ']'); expectAtom = 0; prevWasParenGroup = 0; continue; }
                if (c == '\'' || c == '"' || c == '`') { skipStringNoEmit(); expectAtom = 0; prevWasParenGroup = 0; continue; }
                if (c == '-' || c == '+') { i++; continue; }             // literal type sign
                if (is_id_start((unsigned char)c) || isdigit((unsigned char)c))
                {
                    while (i < len && is_id((unsigned char)s[i])) i++;
                    while (i < len && (isdigit((unsigned char)s[i]) || s[i] == '.')) i++; // number literal types
                    expectAtom = 0; prevWasParenGroup = 0; continue;
                }
                return;   // unexpected -> type ended
            }
            else
            {
                if (c == '[') { skipBalanced('[', ']'); continue; }     // T[] or index
                if (c == '<') { skipBalanced('<', '>'); continue; }     // T<...>
                if (c == '.') { i++; expectAtom = 1; prevWasParenGroup = 0; continue; }  // A.B
                if (c == '|' || c == '&') { i++; expectAtom = 1; prevWasParenGroup = 0; continue; }
                // `=>` continues the type only for a function type `(...) => T`; otherwise
                // it is an arrow-function body after a return-type annotation -> stop.
                if (c == '=' && at(1) == '>') { if (!prevWasParenGroup) return; i += 2; expectAtom = 1; continue; }
                // keyword operators within types
                if (matchWord("extends") || matchWord("keyof") || matchWord("typeof") ||
                    matchWord("infer") || matchWord("readonly") || matchWord("in"))
                {
                    while (i < len && is_id((unsigned char)s[i])) i++; expectAtom = 1; continue;
                }
                return;   // end of type
            }
        }
    }

    // strip an enum body: `enum Name { A, B = 2, C }` -> `const Name = { A:0, B:2, C:3 }`
    void convertEnum()
    {
        i += 4;                                   // 'enum'
        skipWs();
        // (const enum handled by caller stripping 'const' normally)
        size_t ns = i; while (i < len && is_id((unsigned char)s[i])) i++;
        char name[128]; size_t nl = i - ns; if (nl > 127) nl = 127; memcpy(name, s + ns, nl); name[nl] = 0;
        skipWs();
        if (cur() != '{') { puts_("const ", 6); puts_(name, strlen(name)); return; }
        i++;                                      // '{'
        puts_("const ", 6); puts_(name, strlen(name)); puts_(" = {", 4);
        long next = 0; int first = 1;
        for (;;)
        {
            skipWs();
            if (i >= len || cur() == '}') { if (cur() == '}') i++; break; }
            if (cur() == ',') { i++; continue; }
            // member name (identifier or string)
            char mname[160]; size_t ml = 0;
            if (cur() == '\'' || cur() == '"') { char q = s[i]; mname[ml++] = q; i++; while (i < len && s[i] != q) { mname[ml++] = s[i++]; } mname[ml++] = q; if (i < len) i++; }
            else { while (i < len && is_id((unsigned char)s[i]) && ml < 150) mname[ml++] = s[i++]; }
            mname[ml] = 0;
            skipWs();
            if (!first) putc_(',');
            first = 0;
            putc_(' '); puts_(mname, strlen(mname)); putc_(':');
            if (cur() == '=')
            {
                i++; skipWs();
                // copy the value expression up to ',' or '}' at depth 0
                int d = 0; size_t vs = i;
                while (i < len) { char c = s[i]; if ((c == ',' || c == '}') && d == 0) break; if (c=='('||c=='['||c=='{') d++; else if (c==')'||c==']'||c=='}') d--; i++; }
                puts_(s + vs, i - vs);
                // best-effort: bump auto counter if it was a simple integer
                char *endp = nullptr; long v = strtol(s + vs, &endp, 10); if (endp != s + vs) next = v + 1;
            }
            else { char buf[24]; int bl = snprintf(buf, sizeof(buf), "%ld", next++); puts_(buf, bl); }
        }
        puts_(" }", 2);
        prevValue = 1;
    }

    // one step of normal processing
    void step()
    {
        char c = s[i];

        // comments
        if (c == '/' && at(1) == '/') { copyLineComment(); return; }
        if (c == '/' && at(1) == '*') { copyBlockComment(); return; }
        // strings / templates
        if (c == '\'' || c == '"' || c == '`') { copyString(c); return; }

        // whitespace
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { putc_(c); i++; return; }

        // identifiers / keywords
        if (is_id_start((unsigned char)c))
        {
            // statement-level type constructs
            if (matchWord("interface")) { i += 9; skipWs(); while (i < len && is_id((unsigned char)s[i])) i++; skipWs();
                // optional `extends X, Y` before the body
                while (i < len && s[i] != '{' && s[i] != '\n' && s[i] != ';') i++;
                if (cur() == '{') skipBalanced('{', '}'); return; }
            if (matchWord("type") && [&]{ size_t j = i + 4; while (j < len && (s[j]==' '||s[j]=='\t')) j++; return j < len && is_id_start((unsigned char)s[j]); }())
            {
                // `type X ... = ... ;`  (skip to ';' at depth 0)
                i += 4; int d = 0;
                while (i < len) { char c2 = s[i]; if (c2 == '\'' || c2 == '"' || c2 == '`') { skipStringNoEmit(); continue; } if (c2=='('||c2=='['||c2=='{'||c2=='<') d++; else if (c2==')'||c2==']'||c2=='}'||c2=='>') d--; else if (c2 == ';' && d <= 0) { i++; break; } else if (c2 == '\n' && d <= 0) { break; } i++; }
                return;
            }
            if ((matchWord("import") || matchWord("export")))
            {
                size_t j = i + 6; while (j < len && (s[j]==' '||s[j]=='\t')) j++;
                if (j + 4 <= len && strncmp(s + j, "type", 4) == 0 && (j+4>=len || !is_id((unsigned char)s[j+4])))
                {
                    // `import type ...` / `export type ...` -> drop the statement line
                    while (i < len && s[i] != '\n' && s[i] != ';') { if (s[i]=='\''||s[i]=='"'||s[i]=='`') { skipStringNoEmit(); continue; } i++; }
                    return;
                }
            }
            if (matchWord("enum")) { convertEnum(); return; }

            // access modifiers / declare / abstract -> drop the keyword
            if (matchWord("public") || matchWord("private") || matchWord("protected") ||
                matchWord("readonly") || matchWord("abstract") || matchWord("declare") || matchWord("override"))
            {
                while (i < len && is_id((unsigned char)s[i])) i++;   // drop word
                skipWs(0);                                            // and following spaces
                return;
            }
            // `as Type` cast -> drop
            if (matchWord("as"))
            {
                size_t save = i; i += 2; skipWs(0);
                if (i < len && (is_id_start((unsigned char)s[i]) || s[i]=='(' || s[i]=='{' || s[i]=='\'' || s[i]=='"')) { skipType(); return; }
                i = save;   // not a cast (e.g. identifier 'as' unlikely) -> emit normally
            }

            size_t st = i; while (i < len && is_id((unsigned char)s[i])) i++;
            puts_(s + st, i - st);
            prevValue = 1;
            return;
        }

        if (isdigit((unsigned char)c)) { while (i < len && (is_id((unsigned char)s[i]) || s[i]=='.')) putc_(s[i++]); prevValue = 1; return; }

        // punctuation
        switch (c)
        {
        case '{': stk[sp < 511 ? sp++ : sp] = { '{', !prevValue, 0 }; putc_(c); i++; prevValue = 0; return;
        case '(': stk[sp < 511 ? sp++ : sp] = { '(', 0, 0 }; putc_(c); i++; prevValue = 0; return;
        case '[': stk[sp < 511 ? sp++ : sp] = { '[', 0, 0 }; putc_(c); i++; prevValue = 0; return;
        case '}': case ')': case ']': if (sp > 0) sp--; putc_(c); i++; prevValue = (c != '}'); return;
        case '?':
        {
            if (at(1) == '.' || at(1) == '?') { putc_(c); putc_(s[i+1]); i += 2; prevValue = (s[i-1]=='.'); return; }
            // optional member `x?:` -> drop the '?', following ':' becomes annotation
            size_t j = i + 1; while (j < len && (s[j]==' '||s[j]=='\t')) j++;
            if (j < len && s[j] == ':') { i++; return; }
            if (sp > 0 && stk[sp-1].br == '{') { /* obj/block */ }
            if (sp > 0) stk[sp-1].ternary++; else s_topTernary++;
            putc_(c); i++; prevValue = 0; return;
        }
        case ':':
        {
            int ternary = (sp > 0) ? (stk[sp-1].ternary > 0) : (s_topTernary > 0);
            int inObject = (sp > 0 && stk[sp-1].br == '{' && stk[sp-1].isObject);
            if (ternary) { if (sp > 0) stk[sp-1].ternary--; else s_topTernary--; putc_(c); i++; prevValue = 0; return; }
            if (inObject) { putc_(c); i++; prevValue = 0; return; }     // object literal key
            // type annotation -> drop ':' and the type
            i++; skipType(); return;
        }
        case '!':
            // non-null assertion: value '!' not followed by '=' -> drop
            if (prevValue && at(1) != '=') { i++; return; }
            putc_(c); i++; prevValue = 0; return;
        case '<':
            // generic type args after a value: `ident<...>(` or `ident<...>`
            if (prevValue)
            {
                size_t save = i; int d = 0; size_t j = i;
                while (j < len) { char cc = s[j]; if (cc=='<') d++; else if (cc=='>') { d--; if (d==0){ j++; break; } } else if (cc==';'||cc=='{'||cc=='('&&d==0) { break; } j++; }
                if (d == 0 && j > i + 1) { size_t k = j; while (k < len && (s[k]==' '||s[k]=='\t')) k++;
                    if (k < len && (s[k]=='(' || s[k]=='`' || s[k]=='>' )) { i = j; return; } }  // looked like type args -> drop
                i = save;
            }
            putc_(c); i++; prevValue = 0; return;
        default:
            putc_(c); i++;
            prevValue = 0;
            return;
        }
    }

    int s_topTernary = 0;

    char *run(const char *src, size_t slen, size_t *outlen)
    {
        s = src; i = 0; len = slen; sb_init(&out);
        while (i < len) step();
        putc_('\0'); out.n--;   // null-terminate but don't count it
        if (outlen) *outlen = out.n;
        return out.p;
    }
};

} // namespace

extern "C" char *vbjs_ts_to_js(const char *src, size_t *outlen)
{
    TS ts;
    return ts.run(src, strlen(src), outlen);
}
