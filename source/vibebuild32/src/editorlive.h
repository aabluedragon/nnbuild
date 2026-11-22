#ifndef VIBEBUILD32_EDITORLIVE_H
#define VIBEBUILD32_EDITORLIVE_H

#include "editorboard.h"

#include <array>
#include <deque>
#include <memory>

namespace DukeEditor {

constexpr int maxEditors = 32;
constexpr uint16_t editorPort = 23514;

enum class LiveMode { Offline, Connecting, Hosting, Joined };
enum class ObjectKind : uint8_t { Nothing, Sector, Wall, Vertex, Sprite, Start };
enum class Activity : uint8_t { Select, DrawRoom, PlaceSprite, PlaceStart, Drag, Playtest };

struct Presence
{
    ObjectKind kind{};
    Activity activity{};
    int16_t index = -1;
    bool cursor = false, perspective = false;
    uint32_t revision = 0;
    vec2_t position{};           // Plan cursor or 3D camera in Build units.
    uint16_t viewX = 0, viewY = 0; // Normalized coordinates in a 3D viewport.
};

struct Collaborator
{
    std::string name;
    bool ai = false;
    uint32_t generation = 0;
    Presence presence;
};

struct ObjectRef
{
    ObjectKind kind = ObjectKind::Nothing;
    int index = -1;
    bool operator==(ObjectRef const &other) const { return kind == other.kind && index == other.index; }
};

struct ChangeRecord
{
    uint64_t revision = 0;
    std::string author, label;
    int slot = -1;
    bool ai = false, structure = false;
    std::vector<ObjectRef> touched, removed;
    uint32_t restoredEdit = 0;
    bool replayed = false;
};

ChangeRecord DescribeChange(Board const &before, Board const &after, uint64_t revision,
                            std::string author, std::string label, int slot = -1, bool ai = false);

bool InitializeEditorNetwork();

class LiveSession
{
public:
    LiveSession();
    ~LiveSession();
    bool host(uint16_t port, std::string name, uint32_t edition, int artLimit, const Board &board);
    bool join(std::string address, std::string name, uint32_t edition, bool ai = false);
    void leave();
    void tick();
    bool submit(uint32_t base, const Board &board, std::string const &label = "Edit map",
                bool strict = false, bool assisted = false);
    bool useHistory(uint32_t base, bool redo);
    void updatePresence(const Presence &presence);

    LiveMode mode() const;
    int localSlot() const;
    int artLimit() const;
    bool pending() const;
    bool undoable() const;
    bool redoable() const;
    uint32_t revision() const;
    uint32_t presentation() const;
    int author() const;
    const Board &board() const;
    const Board *boardAtRevision(uint32_t revision) const;
    const std::array<Collaborator, maxEditors> &players() const;
    const std::deque<ChangeRecord> &changes() const;
    std::string takeNotice();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace DukeEditor

#endif
