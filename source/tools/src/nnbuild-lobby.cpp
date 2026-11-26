// nnbuild-lobby: the lobby server (masterserver) as a small program of its own.
//
// `nnbuild --headless -masterserver` runs the same lobby, but needs the whole
// game program and the display and sound libraries it is linked with. This one
// needs neither, which suits a server that only keeps the list.
//
//     nnbuild-lobby [port]        (UDP, IPv4 and IPv6; 23519 without a number)
//
// Every event is written to standard error with its date and time.

#include "compat.h"

// The lobby is one source file shared with the game.
#include "../../nnbuild/src/masterserver.cpp"

int main(int argc, char **argv)
{
    int32_t port = 0;

    for (int i = 1; i < argc; i++)
    {
        char *end = NULL;
        long const value = strtol(argv[i], &end, 10);
        if (argv[i][0] && end && !*end && value > 0 && value < 65536)
        {
            port = (int32_t)value;
            continue;
        }

        fprintf(stderr, "Usage: %s [port]\n"
                        "Runs an NNBuild lobby server on the given UDP port (default %d),\n"
                        "over IPv4 and IPv6, until the process is stopped.\n",
                argv[0], MASTERSERVER_DEFAULT_PORT);
        return !(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help") || !strcmp(argv[i], "-?"));
    }

    engineCreateAllocator();   // the log writer allocates through the engine's allocator

    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = false;
    loguru::g_preamble_file = false;

    Masterserver_Run(port);
    return 1;   // only reached when the port could not be opened
}
