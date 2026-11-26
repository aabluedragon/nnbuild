#ifndef NNBUILD_EDITORMCP_H
#define NNBUILD_EDITORMCP_H

int MapEditor_RunMcp(int argc, const char *const *argv);
bool Mcp_ParseOptions(int &argc, const char *const *&argv);
bool Mcp_Start();
void Mcp_Poll();
bool Mcp_UsesStdin();
void Mcp_SetNativeEditor();

#endif
