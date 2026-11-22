#ifndef VIBEBUILD32_EDITORPROJECT_H
#define VIBEBUILD32_EDITORPROJECT_H

#include <cstdint>
#include <string>
#include <vector>

namespace DukeEditor {

struct Board;

enum class GameTarget { Inherit, Duke13, DukeAtomic, Nam, Napalm, Ww2gi, Standalone };

struct TargetProfile
{
    GameTarget target;
    const char *id, *label, *icon, *archive;
    uint32_t crc;
    int tiles;
};

extern TargetProfile const targetProfiles[7];
TargetProfile const &Target(GameTarget target);
GameTarget ParseTarget(const char *id);
GameTarget RunningTarget();

struct ScriptAttachment
{
    std::string kind, script;
    int index = -1;

    bool operator==(ScriptAttachment const &other) const
    {
        return kind == other.kind && script == other.script && index == other.index;
    }
};

struct MapSettings
{
    GameTarget target = GameTarget::Inherit;
    int modern = -1; // Inherit, off, or on.
    std::vector<ScriptAttachment> scripts;
};

struct ProjectSettings
{
    std::string name, entry;
    GameTarget target = GameTarget::DukeAtomic;
    bool modern = false;
};

struct ProjectAsset
{
    std::string path;
    bool directory;
};

constexpr char projectManifest[] = "vibebuild.project.json";
constexpr char mapSettingsSuffix[] = ".vbmap.json";
constexpr unsigned maxScriptAttachments = 1024;
constexpr char modernCompatibilityWarning[] =
    "Modern Mode requires VibeBuild32. This project is incompatible with other source ports and the original game.";
bool ValidAssetPath(std::string const &path);
bool IsMapAsset(std::string const &path);
bool IsScriptAsset(std::string const &path);
bool ReadProject(std::string const &folder, ProjectSettings &settings, std::string &error);
bool WriteProject(std::string const &folder, ProjectSettings const &settings, std::string &error);
bool CreateProject(std::string const &folder, ProjectSettings const &settings, std::string &error);
bool ReadMapSettings(std::string const &map, MapSettings &settings, std::string &error);
bool WriteMapSettings(std::string const &map, MapSettings const &settings, std::string &error);
bool WriteProjectMap(std::string const &map, Board const &board, int tileLimit,
                     MapSettings const &settings, std::string &error);
bool CopyProjectAsset(std::string const &source, std::string const &destination, bool overwrite, std::string &error);
std::vector<ProjectAsset> ListProjectAssets(std::string const &folder);
bool IsProjectArchive(std::string const &archive);
bool ExportProject(std::string const &folder, std::string const &archive, bool overwrite, std::string &error);
bool ImportProject(std::string const &archive, std::string const &folder, std::string &error);
std::string ImportTemporaryProject(std::string const &archive, std::string &error);
void RemoveTemporaryProject(std::string const &folder);

// The actual game launch uses the source port's existing data and map options.
bool PrepareProjectLaunch(int &argc, const char *const *&argv);
void SelectProjectGame();
bool ProjectStandalone();
bool ProjectAllowsAutoScripts();
bool ValidateProjectGame();
std::string const &LaunchProjectFolder();
void ActivateProjectScripts(std::string const &folder, std::string const &map, MapSettings const &settings);
void ActivateLaunchProjectScripts();

} // namespace DukeEditor

#endif
