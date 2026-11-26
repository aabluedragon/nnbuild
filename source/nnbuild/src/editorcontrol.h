#ifndef NNBUILD_EDITORCONTROL_H
#define NNBUILD_EDITORCONTROL_H

#include "editortools.h"

namespace DukeEditor {

enum class WorkspaceAction { Enter, New, Open, Save, Undo, Redo, Play, Stop, Close, Plan, Perspective, Frame, Host, Join, Leave, BuildTools, Section };

struct WorkspaceState
{
    Board const *board = nullptr;
    LiveSession const *session = nullptr;
    char const *name = "";
    char const *path = "";
    uint64_t revision = 0;
    int tileLimit = 0;
    bool playing = false, dirty = false, busy = false, undoable = false, redoable = false, perspective = false;
    bool section = false;
    Presence presence;
    std::deque<ChangeRecord> const *changes = nullptr;
};

struct WorkspaceRequest
{
    WorkspaceAction action;
    std::string path, address, name;
    GameTarget target = GameTarget::Inherit;
    int port = 0;
    bool discard = false, overwrite = false;
    PlayPoint playFrom;
};

WorkspaceState GetWorkspace();
std::vector<MapIssue> CheckWorkspaceMap();
bool ControlWorkspace(WorkspaceRequest const &request, std::string &error);
bool PointInWorkspace(Presence const &presence, bool setSelection, std::string &error);
bool EditWorkspace(uint64_t expectedRevision, Board next, Presence const &presence, std::string &error,
                   std::string const &label, bool strict);

enum class ProjectAction { Create, Open, Close, Import, Export, AddAsset, Settings, MapSettings, Attach, Detach, ExportMap,
                           CreateComponent, SavePrefab, PlacePrefab };

struct ProjectRequest
{
    explicit ProjectRequest(ProjectAction value = ProjectAction::Open) : action(value) {}
    ProjectAction action;
    std::string path, destination, name, entry, kind, script;
    std::vector<int> sprites;
    GameTarget target = GameTarget::Inherit;
    int modern = -2, index = -1;
    vec2_t position{};
    int sector = -1, floorLayer = 0;
    bool useFloorLayer = false;
    bool overwrite = false, setEntry = false, setTarget = false;
};

struct WorkspaceAsset
{
    ProjectAsset asset;
    GameTarget target;
};

struct ProjectWorkspace
{
    std::string root;
    ProjectSettings project;
    MapSettings map;
    GameTarget effectiveTarget = GameTarget::Inherit;
    bool modern = false;
    std::vector<WorkspaceAsset> assets;
};

ProjectWorkspace const &GetProjectWorkspace();
bool ControlProject(ProjectRequest const &request, std::string &error);

} // namespace DukeEditor

#endif
