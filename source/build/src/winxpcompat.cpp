// Windows XP support for the 32-bit Windows build.
//
// The program, and the libraries built into it (QuickJS, mimalloc, loguru, the
// C++ runtime), call ten Windows functions that Windows XP does not have. A
// program that imports a function the system lacks does not start at all. This
// file therefore provides those ten names itself, so none of them is imported:
// each looks the real function up when it is first called and passes the call
// on, and only where the system has no such function does the replacement
// below run. On Windows Vista and later nothing changes.
//
// Only the 32-bit build includes this file (see engine_excl in GNUmakefile).
// It also sets its minimum Windows version to 5.1 (Common.mak).
//
// To run the replacements on a newer Windows, for testing, set the environment
// variable NNB_XP_FALLBACKS=1 before starting the program.

#if defined _WIN32 && !defined _WIN64

#define WIN32_LEAN_AND_MEAN
// Declare only what Windows XP has, so the newer functions' names are free.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

extern "C" {

// The real function in a library that is already loaded, or NULL when the system
// has none (or the test variable asks for the replacements).
static FARPROC nnbxp_find(const char *library, const char *name)
{
    static LONG forced = -1;
    if (forced < 0)
    {
        char value[8];
        DWORD const length = GetEnvironmentVariableA("NNB_XP_FALLBACKS", value, sizeof(value));
        InterlockedExchange(&forced, length == 1 && value[0] == '1');
    }
    if (forced)
        return NULL;

    HMODULE const module = GetModuleHandleA(library);
    return module ? GetProcAddress(module, name) : NULL;
}

// Looks the real function up once. Two threads doing it at the same time both
// find the same answer.
#define NNBXP_REAL(type, library, name)                                  \
    static type real;                                                    \
    static LONG looked;                                                  \
    if (!looked)                                                         \
    {                                                                    \
        real = (type)(void (*)(void))nnbxp_find(library, name);          \
        InterlockedExchange(&looked, 1);                                 \
    }

// Makes `name` (a function taking `bytes` bytes of arguments, callee cleans up)
// resolve to nnbxp_<name>, both for callers that go through the import pointer
// and for callers that call the name directly.
#define NNBXP_STDCALL(name, bytes)                                                         \
    __asm__(".globl _" #name "@" #bytes "\n"                                               \
            ".set _" #name "@" #bytes ", _nnbxp_" #name "@" #bytes "\n"                    \
            ".section .rdata$nnbxp,\"dr\"\n"                                               \
            ".globl __imp__" #name "@" #bytes "\n"                                         \
            ".p2align 2\n"                                                                 \
            "__imp__" #name "@" #bytes ": .long _nnbxp_" #name "@" #bytes "\n"             \
            ".text\n");

// The same for a C runtime function (the caller cleans up).
#define NNBXP_CDECL(name)                                                                  \
    __asm__(".globl _" #name "\n"                                                          \
            ".set _" #name ", _nnbxp_" #name "\n"                                          \
            ".section .rdata$nnbxp,\"dr\"\n"                                               \
            ".globl __imp__" #name "\n"                                                    \
            ".p2align 2\n"                                                                 \
            "__imp__" #name ": .long _nnbxp_" #name "\n"                                   \
            ".text\n");

#define NNBXP_USED __attribute__((used))


//// GetTickCount64

NNBXP_USED ULONGLONG WINAPI nnbxp_GetTickCount64(void)
{
    typedef ULONGLONG (WINAPI *fn_t)(void);
    NNBXP_REAL(fn_t, "kernel32.dll", "GetTickCount64");
    if (real)
        return real();

    // The 32-bit counter wraps every 49.7 days; count the wraps.
    static LONG busy;
    static DWORD last, wraps;
    while (InterlockedCompareExchange(&busy, 1, 0) != 0)
        Sleep(0);
    DWORD const now = GetTickCount();
    if (now < last)
        wraps++;
    last = now;
    ULONGLONG const ticks = ((ULONGLONG)wraps << 32) | now;
    InterlockedExchange(&busy, 0);
    return ticks;
}
NNBXP_STDCALL(GetTickCount64, 0)


//// InitOnceExecuteOnce

// INIT_ONCE: one pointer, zero before first use.
typedef BOOL (WINAPI *nnbxp_once_fn)(void *once, void *parameter, void **context);

NNBXP_USED BOOL WINAPI nnbxp_InitOnceExecuteOnce(void *once, nnbxp_once_fn function, void *parameter, void **context)
{
    typedef BOOL (WINAPI *fn_t)(void *, nnbxp_once_fn, void *, void **);
    NNBXP_REAL(fn_t, "kernel32.dll", "InitOnceExecuteOnce");
    if (real)
        return real(once, function, parameter, context);

    // 0: not run, 1: running in some thread, 2: done. The value a callback stores
    // through its context pointer is not kept (no caller here uses one).
    LONG volatile *const state = (LONG volatile *)once;
    for (;;)
    {
        LONG const was = InterlockedCompareExchange(state, 1, 0);
        if (was == 2)
        {
            if (context)
                *context = NULL;
            return TRUE;
        }
        if (was == 0)
        {
            void *unused = NULL;
            BOOL const ok = function(once, parameter, &unused);
            InterlockedExchange(state, ok ? 2 : 0);
            if (context)
                *context = NULL;
            return ok;
        }
        Sleep(1);
    }
}
NNBXP_STDCALL(InitOnceExecuteOnce, 16)


//// Condition variables

// CONDITION_VARIABLE: one pointer, zero before first use. The replacement keeps
// its state in a block it allocates on the first wait. Windows has no call that
// ends a condition variable, so that block lives until the program ends.
struct nnbxp_condition
{
    CRITICAL_SECTION lock;
    HANDLE wake;     // a semaphore: one count per thread being woken
    LONG waiting;    // threads waiting that no wake-up has been counted for yet
};

static nnbxp_condition *nnbxp_condition_get(void **slot)
{
    nnbxp_condition *condition = (nnbxp_condition *)*slot;
    if (condition)
        return condition;

    condition = (nnbxp_condition *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*condition));
    if (!condition)
        return NULL;
    condition->wake = CreateSemaphoreA(NULL, 0, 0x7fffffff, NULL);
    if (!condition->wake)
    {
        HeapFree(GetProcessHeap(), 0, condition);
        return NULL;
    }
    InitializeCriticalSection(&condition->lock);

    void *const other = InterlockedCompareExchangePointer(slot, condition, NULL);
    if (other)  // another thread was first
    {
        DeleteCriticalSection(&condition->lock);
        CloseHandle(condition->wake);
        HeapFree(GetProcessHeap(), 0, condition);
        return (nnbxp_condition *)other;
    }
    return condition;
}

NNBXP_USED void WINAPI nnbxp_InitializeConditionVariable(void **slot)
{
    typedef void (WINAPI *fn_t)(void **);
    NNBXP_REAL(fn_t, "kernel32.dll", "InitializeConditionVariable");
    if (real)
        return real(slot);
    *slot = NULL;
}
NNBXP_STDCALL(InitializeConditionVariable, 4)

NNBXP_USED BOOL WINAPI nnbxp_SleepConditionVariableCS(void **slot, CRITICAL_SECTION *mutex, DWORD milliseconds)
{
    typedef BOOL (WINAPI *fn_t)(void **, CRITICAL_SECTION *, DWORD);
    NNBXP_REAL(fn_t, "kernel32.dll", "SleepConditionVariableCS");
    if (real)
        return real(slot, mutex, milliseconds);

    nnbxp_condition *const condition = nnbxp_condition_get(slot);
    if (!condition)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    // Count this thread as waiting before the caller's mutex is released, so a
    // wake-up sent right after the release is not lost.
    EnterCriticalSection(&condition->lock);
    condition->waiting++;
    LeaveCriticalSection(&condition->lock);

    LeaveCriticalSection(mutex);
    BOOL woken = WaitForSingleObject(condition->wake, milliseconds) == WAIT_OBJECT_0;
    if (!woken)
    {
        // Timed out. A wake-up may have been counted for this thread meanwhile:
        // then take it, otherwise stop counting as waiting.
        EnterCriticalSection(&condition->lock);
        if (WaitForSingleObject(condition->wake, 0) == WAIT_OBJECT_0)
            woken = TRUE;
        else
            condition->waiting--;
        LeaveCriticalSection(&condition->lock);
    }
    EnterCriticalSection(mutex);

    if (!woken)
        SetLastError(ERROR_TIMEOUT);
    return woken;
}
NNBXP_STDCALL(SleepConditionVariableCS, 12)

static void nnbxp_condition_wake(void **slot, bool all)
{
    nnbxp_condition *const condition = (nnbxp_condition *)*slot;
    if (!condition)  // nothing has ever waited on it
        return;

    EnterCriticalSection(&condition->lock);
    LONG const count = all ? condition->waiting : (condition->waiting > 0 ? 1 : 0);
    if (count > 0)
    {
        condition->waiting -= count;
        ReleaseSemaphore(condition->wake, count, NULL);
    }
    LeaveCriticalSection(&condition->lock);
}

NNBXP_USED void WINAPI nnbxp_WakeConditionVariable(void **slot)
{
    typedef void (WINAPI *fn_t)(void **);
    NNBXP_REAL(fn_t, "kernel32.dll", "WakeConditionVariable");
    if (real)
        return real(slot);
    nnbxp_condition_wake(slot, false);
}
NNBXP_STDCALL(WakeConditionVariable, 4)

NNBXP_USED void WINAPI nnbxp_WakeAllConditionVariable(void **slot)
{
    typedef void (WINAPI *fn_t)(void **);
    NNBXP_REAL(fn_t, "kernel32.dll", "WakeAllConditionVariable");
    if (real)
        return real(slot);
    nnbxp_condition_wake(slot, true);
}
NNBXP_STDCALL(WakeAllConditionVariable, 4)


//// Processor information (used by mimalloc)

NNBXP_USED DWORD WINAPI nnbxp_GetCurrentProcessorNumber(void)
{
    typedef DWORD (WINAPI *fn_t)(void);
    NNBXP_REAL(fn_t, "kernel32.dll", "GetCurrentProcessorNumber");
    return real ? real() : 0;
}
NNBXP_STDCALL(GetCurrentProcessorNumber, 0)

NNBXP_USED SIZE_T WINAPI nnbxp_GetLargePageMinimum(void)
{
    typedef SIZE_T (WINAPI *fn_t)(void);
    NNBXP_REAL(fn_t, "kernel32.dll", "GetLargePageMinimum");
    return real ? real() : 0;  // 0: the system has no large pages
}
NNBXP_STDCALL(GetLargePageMinimum, 0)


//// inet_ntop

NNBXP_USED const char *WSAAPI nnbxp_inet_ntop(int family, const void *address, char *text, size_t size)
{
    typedef const char *(WSAAPI *fn_t)(int, const void *, char *, size_t);
    NNBXP_REAL(fn_t, "ws2_32.dll", "inet_ntop");
    if (real)
        return real(family, address, text, size);

    char buffer[64];
    if (family == AF_INET)
    {
        unsigned char const *const b = (unsigned char const *)address;
        snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    }
    else if (family == AF_INET6)
    {
        // The system writes the short form (::1). It refuses when IPv6 is not
        // installed; then write all eight groups, which reads back the same.
        struct sockaddr_in6 in6;
        memset(&in6, 0, sizeof(in6));
        in6.sin6_family = AF_INET6;
        memcpy(&in6.sin6_addr, address, 16);
        DWORD length = sizeof(buffer);
        if (WSAAddressToStringA((struct sockaddr *)&in6, sizeof(in6), NULL, buffer, &length) != 0)
        {
            unsigned char const *const b = (unsigned char const *)address;
            snprintf(buffer, sizeof(buffer), "%x:%x:%x:%x:%x:%x:%x:%x",
                     (b[0] << 8) | b[1], (b[2] << 8) | b[3], (b[4] << 8) | b[5], (b[6] << 8) | b[7],
                     (b[8] << 8) | b[9], (b[10] << 8) | b[11], (b[12] << 8) | b[13], (b[14] << 8) | b[15]);
        }
    }
    else
    {
        WSASetLastError(WSAEAFNOSUPPORT);
        return NULL;
    }

    size_t const needed = strlen(buffer) + 1;
    if (!text || needed > size)
    {
        WSASetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    memcpy(text, buffer, needed);
    return text;
}
NNBXP_STDCALL(inet_ntop, 16)


//// strncpy_s, strncat_s (used by loguru)

// What the real ones return when they had to cut the text (STRUNCATE).
enum { NNBXP_TRUNCATED = 80 };

NNBXP_USED int __cdecl nnbxp_strncpy_s(char *destination, size_t size, const char *source, size_t count)
{
    typedef int (__cdecl *fn_t)(char *, size_t, const char *, size_t);
    NNBXP_REAL(fn_t, "msvcrt.dll", "strncpy_s");
    if (real)
        return real(destination, size, source, count);

    if (!destination || !size)
        return EINVAL;
    if (!source)
    {
        destination[0] = 0;
        return EINVAL;
    }

    size_t i = 0;
    while (i < count && source[i] && i + 1 < size)
    {
        destination[i] = source[i];
        i++;
    }
    destination[i] = 0;
    if (i < count && source[i])  // it did not fit
    {
        if (count == (size_t)-1)  // _TRUNCATE: keep what fitted
            return NNBXP_TRUNCATED;
        destination[0] = 0;
        return ERANGE;
    }
    return 0;
}
NNBXP_CDECL(strncpy_s)

NNBXP_USED int __cdecl nnbxp_strncat_s(char *destination, size_t size, const char *source, size_t count)
{
    typedef int (__cdecl *fn_t)(char *, size_t, const char *, size_t);
    NNBXP_REAL(fn_t, "msvcrt.dll", "strncat_s");
    if (real)
        return real(destination, size, source, count);

    if (!destination || !size)
        return EINVAL;
    size_t used = 0;
    while (used < size && destination[used])
        used++;
    if (used == size || !source)  // no end of text inside the buffer, or nothing to add
    {
        destination[0] = 0;
        return EINVAL;
    }

    size_t i = 0;
    while (i < count && source[i] && used + i + 1 < size)
    {
        destination[used + i] = source[i];
        i++;
    }
    destination[used + i] = 0;
    if (i < count && source[i])  // it did not fit
    {
        if (count == (size_t)-1)
            return NNBXP_TRUNCATED;
        destination[0] = 0;
        return ERANGE;
    }
    return 0;
}
NNBXP_CDECL(strncat_s)

}  // extern "C"

#endif  // 32-bit Windows
