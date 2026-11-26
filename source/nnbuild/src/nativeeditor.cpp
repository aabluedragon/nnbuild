#include "nativeeditor.h"

#include "duke3d.h"
#include "editormcp.h"
#include "netcontent.h"
#include "oldnet.h"

#if defined(NNBUILD_NATIVE_EDITOR) && !defined(_WIN32)
# include <sys/wait.h>
# include <unistd.h>
#endif

#ifdef NNBUILD_NATIVE_EDITOR
bool g_nativeEditor;

extern "C" int32_t NativeEditor_handleevents()
{
    Mcp_Poll();
    return handleevents();
}
#endif

bool NativeEditor_Available()
{
#ifdef NNBUILD_NATIVE_EDITOR
    return true;
#else
    return false;
#endif
}

intptr_t NativeEditor_Launch(std::string const &folder, std::string const &map, int mcpPort, std::string &error)
{
#ifdef NNBUILD_NATIVE_EDITOR
    auto const exe = Net_ProcessExecutable();
    std::string archive;
    if (!exe || !Net_ResolveContentPath(G_GrpFile(), archive))
    {
        error = "Could not locate the game executable or the selected game archive.";
        return -1;
    }
    auto const search = std::string("-j") + Net_LaunchDirectory();
    auto const port = std::to_string(mcpPort);
    std::vector<const char *> args{exe, "--build-editor", "-usecwd", "-noautoload", "-gamegrp", archive.c_str(), search.c_str()};
    if (mcpPort)
    {
        args.push_back("--mcp-http");
        args.push_back(port.c_str());
    }
    args.push_back(map.c_str());
    args.push_back(nullptr);
#ifdef _WIN32
    auto const process = Net_StartContentProcess(args.data(), folder.c_str());
    if (process != -1)
        return process;
#else
    std::vector<char *> arguments;
    for (auto const arg : args)
        arguments.push_back(const_cast<char *>(arg));
    auto const directory = folder.c_str();
    auto const argv = arguments.data();
    auto const child = fork();
    if (child == 0)
    {
        if (!chdir(directory))
            execv(exe, argv);
        _exit(EXIT_FAILURE);
    }
    if (child > 0) return child;
#endif
    error = "Could not start the native Build tools.";
#else
    UNREFERENCED_PARAMETER(folder);
    UNREFERENCED_PARAMETER(map);
    UNREFERENCED_PARAMETER(mcpPort);
    error = "This build has no native Build tools.";
#endif
    return -1;
}

bool NativeEditor_Finished(intptr_t &process)
{
#ifdef NNBUILD_NATIVE_EDITOR
    if (process < 0)
        return false;
#ifdef _WIN32
    auto const handle = reinterpret_cast<HANDLE>(process);
    if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT)
        return false;
    CloseHandle(handle);
#else
    if (waitpid(process, nullptr, WNOHANG) == 0)
        return false;
#endif
    process = -1;
    return true;
#else
    UNREFERENCED_PARAMETER(process);
    return false;
#endif
}
