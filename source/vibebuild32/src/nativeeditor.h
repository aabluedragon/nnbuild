#ifndef VIBEBUILD32_NATIVEEDITOR_H
#define VIBEBUILD32_NATIVEEDITOR_H

#include <cstdint>
#include <string>

#ifdef VIBEBUILD_NATIVE_EDITOR
extern bool g_nativeEditor;
extern "C" int NativeEditor_app_main(int argc, const char *const *argv);
extern "C" void NativeEditor_app_crashhandler();
extern "C" void NativeEditor_M32RunScript(const char *command);
#endif

bool NativeEditor_Available();
intptr_t NativeEditor_Launch(std::string const &folder, std::string const &map, int mcpPort, std::string &error);
bool NativeEditor_Finished(intptr_t &process);

#endif
