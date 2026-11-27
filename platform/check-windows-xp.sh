#!/bin/sh
# Check that a 32-bit Windows build can start on Windows XP.
#
# Usage: platform/check-windows-xp.sh <nnbuild.exe> [objdump]
#
# Windows refuses to start a program whose header asks for a newer Windows, or
# that imports a function the system does not have. This checks the header and
# looks for functions newer than Windows XP that the program, or a library
# built into it, is known to reach for. The list below is not every such
# function: when a new one turns up, add it here and give it a stand-in in
# source/build/src/winxpcompat.cpp.
set -eu

exe="${1:?usage: platform/check-windows-xp.sh <nnbuild.exe> [objdump]}"
objdump="${2:-i686-w64-mingw32-objdump}"

newer="
GetTickCount64 InitOnceExecuteOnce InitOnceBeginInitialize InitOnceComplete
InitializeConditionVariable SleepConditionVariableCS SleepConditionVariableSRW
WakeConditionVariable WakeAllConditionVariable
InitializeSRWLock AcquireSRWLockExclusive AcquireSRWLockShared
ReleaseSRWLockExclusive ReleaseSRWLockShared TryAcquireSRWLockExclusive TryAcquireSRWLockShared
GetCurrentProcessorNumber GetLargePageMinimum FlsAlloc FlsFree FlsGetValue FlsSetValue
InitializeCriticalSectionEx CancelIoEx GetFileInformationByHandleEx SetFileInformationByHandle
GetFinalPathNameByHandleA GetFinalPathNameByHandleW CompareStringEx GetLocaleInfoEx LCMapStringEx
QueryFullProcessImageNameA QueryFullProcessImageNameW GetThreadId
GetLogicalProcessorInformationEx GetSystemTimePreciseAsFileTime CreateFile2
inet_ntop inet_pton InetNtopW InetPtonW WSAPoll if_nametoindex if_indextoname
SHGetKnownFolderPath RegGetValueA RegGetValueW
strncpy_s strncat_s strcpy_s strcat_s memcpy_s memmove_s sprintf_s vsprintf_s
_snprintf_s _vsnprintf_s fopen_s _wfopen_s localtime_s gmtime_s rand_s
"

dump=$("$objdump" -p "$exe")
failed=0

major=$(printf '%s\n' "$dump" | awk '$1 == "MajorSubsystemVersion" { print $2; exit }')
minor=$(printf '%s\n' "$dump" | awk '$1 == "MinorSubsystemVersion" { print $2; exit }')
if [ -z "$major" ] || [ -z "$minor" ]; then
    echo "error: $exe: no Windows program header found" >&2
    exit 1
fi
if [ "$major" -gt 5 ] || { [ "$major" -eq 5 ] && [ "$minor" -gt 1 ]; }; then
    echo "error: the header asks for Windows $major.$minor; Windows XP is 5.1" >&2
    failed=1
fi

# Import lines read: <address> <ordinal or "<none>"> <hint> <name>
imports=$(printf '%s\n' "$dump" | awk 'NF == 4 && $1 ~ /^[0-9a-f]+$/ && $3 ~ /^[0-9a-f]+$/ { print $4 }' | sort -u)
for name in $newer; do
    if printf '%s\n' "$imports" | grep -qx "$name"; then
        echo "error: imports $name, which Windows XP does not have" >&2
        failed=1
    fi
done

if [ "$failed" -ne 0 ]; then
    exit 1
fi
echo "$exe: header asks for Windows $major.$minor, $(printf '%s\n' "$imports" | wc -l) imported functions, none of the known newer ones"
