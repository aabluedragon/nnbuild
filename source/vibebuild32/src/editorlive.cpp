#include "editorlive.h"

#include "enet.h"
#include "lz4.h"

#include <chrono>
#include <deque>

namespace DukeEditor {
namespace {

using Clock = std::chrono::steady_clock;
using Bytes = std::vector<uint8_t>;

constexpr uint32_t connectCode = 0x56424503;
constexpr size_t labelLimit = 64;
constexpr size_t packetLimit = maxBoardBytes + maxBoardBytes / 255 + 128;
constexpr int historyLimit = 24;

enum Packet : uint8_t { Hello = 1, Welcome, Player, Left, Cursor, Edit, State, Problem, HistoryRequest };

void number(Bytes &out, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(value >> (8 * i));
}

struct Message
{
    uint8_t const *data;
    size_t remaining;
    bool valid = true;

    uint8_t byte()
    {
        if (!remaining)
        {
            valid = false;
            return 0;
        }
        --remaining;
        return *data++;
    }
    uint32_t word()
    {
        uint32_t result = 0;
        for (int i = 0; i < 4; ++i)
            result |= uint32_t(byte()) << (8 * i);
        return result;
    }
    bool finished() const { return valid && !remaining; }
};

std::string cleanName(std::string const &name)
{
    std::string out;
    for (unsigned char c : name)
        if (c >= 32 && c < 127 && out.size() < 36)
            out.push_back(c);
    return out.empty() ? "Editor" : out;
}

std::string cleanLabel(std::string const &label)
{
    std::string text;
    for (unsigned char c : label)
        if (c >= 32 && c < 127 && text.size() < labelLimit)
            text.push_back(c);
    return text.empty() ? "Edit map" : text;
}

void putLabel(Bytes &bytes, std::string const &label)
{
    auto const text = cleanLabel(label);
    bytes.push_back(text.size());
    bytes.insert(bytes.end(), text.begin(), text.end());
}

std::string readLabel(Message &message)
{
    size_t const length = message.byte();
    if (length > labelLimit || length > message.remaining)
    {
        message.valid = false;
        return {};
    }
    std::string label;
    for (size_t i = 0; i < length; ++i)
    {
        auto const c = message.byte();
        if (c >= 32 && c < 127)
            label.push_back(c);
    }
    return label.empty() ? "Edit map" : label;
}

bool getAddress(std::string input, ENetAddress &address)
{
    address.port = editorPort;
    std::string port;
    if (!input.empty() && input.front() == '[')
    {
        size_t const end = input.find(']');
        if (end == std::string::npos)
            return false;
        if (input.size() > end + 1)
        {
            if (input[end + 1] != ':')
                return false;
            port = input.substr(end + 2);
        }
        input = input.substr(1, end - 1);
    }
    else if (input.find(':') != std::string::npos && input.find(':') == input.rfind(':'))
    {
        auto const split = input.find(':');
        port = input.substr(split + 1);
        input.resize(split);
    }
    if (!port.empty())
    {
        char *end;
        auto const value = strtol(port.c_str(), &end, 10);
        if (*end || value < 1 || value > UINT16_MAX)
            return false;
        address.port = value;
    }
    return !input.empty() && enet_address_set_host(&address, &input[0]) == 0;
}

void ensureLimits(ENetHost *host)
{
    host->maximumPacketSize = packetLimit;
    host->maximumWaitingData = packetLimit * 5;
}

bool send(ENetPeer *peer, Bytes const &data, bool reliable = true)
{
    if (!peer || data.empty())
        return false;
    // ENet copies the packet contents.
    auto const packet = enet_packet_create(const_cast<uint8_t *>(data.data()), data.size(),
                                           reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (packet && enet_peer_send(peer, reliable ? 0 : 1, packet) == 0)
        return true;
    if (packet)
        enet_packet_destroy(packet);
    return false;
}

Bytes textPacket(Packet type, std::string const &text)
{
    Bytes out{type};
    auto const limit = std::min(text.size(), size_t(160));
    out.insert(out.end(), text.begin(), text.begin() + limit);
    return out;
}

Bytes boardPacket(Bytes header, Board const &board)
{
    auto const raw = EncodeBoard(board);
    const int maxCompressed = LZ4_compressBound(raw.size());
    number(header, raw.size());
    size_t const pos = header.size();
    header.resize(pos + maxCompressed);
    int const compressed = LZ4_compress_default(reinterpret_cast<char const *>(raw.data()),
        reinterpret_cast<char *>(header.data() + pos), raw.size(), maxCompressed);
    if (!compressed)
        return {};
    header.resize(pos + compressed);
    return header;
}

bool readBoard(Message &message, Board &out, int artLimit, std::string &error)
{
    auto const size = message.word();
    if (!message.valid || size < 26 || size > maxBoardBytes || !message.remaining)
        return false;
    Bytes raw(size);
    if (LZ4_decompress_safe(reinterpret_cast<char const *>(message.data), reinterpret_cast<char *>(raw.data()),
                            message.remaining, raw.size()) != int(raw.size()))
        return false;
    return DecodeBoard(raw, out, artLimit, error, true);
}

Bytes cursorPacket(int slot, Collaborator const &player)
{
    auto const &p = player.presence;
    Bytes bytes{Cursor, uint8_t(slot), uint8_t(p.kind), uint8_t(p.activity),
                uint8_t((p.cursor ? 1 : 0) | (p.perspective ? 2 : 0))};
    number(bytes, player.generation);
    number(bytes, p.revision);
    number(bytes, p.index);
    number(bytes, p.position.x);
    number(bytes, p.position.y);
    number(bytes, p.viewX);
    number(bytes, p.viewY);
    return bytes;
}

bool readCursor(Message &message, uint32_t &generation, Presence &p)
{
    auto const kind = message.byte();
    auto const activity = message.byte();
    auto const flags = message.byte();
    generation = message.word();
    p.revision = message.word();
    auto const index = int32_t(message.word());
    p.position.x = int32_t(message.word());
    p.position.y = int32_t(message.word());
    auto const x = message.word(), y = message.word();
    if (!message.finished() || kind > uint8_t(ObjectKind::Start) || activity > uint8_t(Activity::Playtest) ||
        index < -1 || index >= MAXWALLSV7 || flags > 3 || x > UINT16_MAX || y > UINT16_MAX ||
        p.position.x < -BXY_MAX || p.position.x > BXY_MAX || p.position.y < -BXY_MAX || p.position.y > BXY_MAX)
        return false;
    p.kind = ObjectKind(kind);
    p.activity = Activity(activity);
    p.index = index;
    p.cursor = (flags & 1) != 0;
    p.perspective = (flags & 2) != 0;
    p.viewX = x; p.viewY = y;
    return true;
}

Bytes playerPacket(int slot, Collaborator const &player)
{
    Bytes out{Player, uint8_t(slot), uint8_t(player.ai)};
    number(out, player.generation);
    out.insert(out.end(), player.name.begin(), player.name.end());
    return out;
}

} // namespace

ChangeRecord DescribeChange(Board const &before, Board const &after, uint64_t revision,
                            std::string author, std::string label, int slot, bool ai)
{
    ChangeRecord change{revision, std::move(author), std::move(label), slot, ai, !SameTopology(before, after), {}, {}};
    auto compare = [&](auto const &old, auto const &next, ObjectKind kind) {
        for (size_t i = 0; i < next.size(); ++i)
            if (i >= old.size() || Bmemcmp(&old[i], &next[i], sizeof(next[i])))
                change.touched.push_back({kind, int(i)});
        for (size_t i = next.size(); i < old.size(); ++i)
            change.removed.push_back({kind, int(i)});
    };
    compare(before.sectors, after.sectors, ObjectKind::Sector);
    compare(before.walls, after.walls, ObjectKind::Wall);
    compare(before.sprites, after.sprites, ObjectKind::Sprite);
    if (Bmemcmp(&before.start, &after.start, sizeof(after.start)) || before.angle != after.angle ||
        before.startSector != after.startSector)
        change.touched.push_back({ObjectKind::Start, 0});
    return change;
}

bool InitializeEditorNetwork()
{
    static bool const ready = enet_initialize() == 0;
    return ready;
}

struct LiveSession::Impl
{
    struct Past { uint32_t revision; Board board; };
    struct Restore { Board board; std::string label; uint32_t edit; };

    ENetHost *link = nullptr;
    ENetPeer *server = nullptr;
    LiveMode mode{};
    int slot = -1, limit = 5120, author = -1;
    uint32_t edition = 0, revision = 0, structureRevision = 0, presentation = 0, nextGeneration = 1;
    bool ai = false, pending = false, canUndo = false, canRedo = false;
    std::string name, notice;
    Board board;
    std::deque<Past> past;
    std::deque<Restore> undo, redo;
    std::deque<ChangeRecord> changes;
    std::string lastLabel;
    bool lastAssisted = false;
    std::array<Collaborator, maxEditors> players;
    std::array<Clock::time_point, maxEditors> helloDeadline{};
    Clock::time_point connecting{}, lastCursor{};
    Bytes lastPresence;

    bool hosting() const { return mode == LiveMode::Hosting; }
    ENetPeer *peer(int id) const { return &link->peers[id - 1]; }

    void broadcast(Bytes const &data, int except = -1, bool reliable = true)
    {
        for (int id = 1; id < maxEditors; ++id)
            if (id != except && !players[id].name.empty())
                send(peer(id), data, reliable);
    }

    void clearStaleSelection(Presence &presence) const
    {
        if (presence.revision < structureRevision)
        {
            presence.kind = ObjectKind::Nothing;
            presence.index = -1;
        }
    }

    void setBoard(Board next, uint32_t rev, int by, std::string label = {}, bool assisted = false)
    {
        label = cleanLabel(label);
        if (revision && rev > revision && by >= 0 && by < maxEditors)
        {
            changes.push_back(DescribeChange(board, next, rev, players[by].name, label, by, assisted || players[by].ai));
            if (changes.size() > historyLimit) changes.pop_front();
        }
        if (!SameTopology(board, next))
        {
            structureRevision = rev;
            for (auto &player : players)
                clearStaleSelection(player.presence);
        }
        board = std::move(next);
        revision = rev;
        author = by;
        lastLabel = std::move(label);
        lastAssisted = assisted;
        ++presentation;
    }

    Bytes snapshot(bool conflict)
    {
        Bytes message{State, uint8_t(author < 0 ? 255 : author),
                      uint8_t(conflict | (!undo.empty() << 1) | (!redo.empty() << 2) | (lastAssisted << 3))};
        number(message, revision);
        putLabel(message, lastLabel);
        return boardPacket(std::move(message), board);
    }

    void publish(Board next, int from, std::string label, bool assisted, uint32_t restoredEdit = 0, bool replayed = false)
    {
        past.push_back({revision, board});
        if (past.size() > historyLimit)
            past.pop_front();
        setBoard(std::move(next), revision + 1, from, std::move(label), assisted);
        changes.back().restoredEdit = restoredEdit;
        changes.back().replayed = replayed;
        canUndo = !undo.empty();
        canRedo = !redo.empty();
        broadcast(snapshot(false));
    }

    bool apply(uint32_t base, Board next, int from, std::string label, bool strict, bool assisted)
    {
        if (base != revision)
        {
            if (strict)
                return false;
            auto const original = std::find_if(past.begin(), past.end(), [&](auto const &p) { return p.revision == base; });
            Board merged;
            if (base < structureRevision || original == past.end() || !MergeBoard(original->board, next, board, merged))
                return false;
            next = std::move(merged);
        }

        std::string error;
        if (!next.validate(limit, error, true) || !next.validateGeometryChange(board, error))
            return false;
        undo.push_back({board, label, revision + 1});
        if (undo.size() > historyLimit) undo.pop_front();
        redo.clear();
        publish(std::move(next), from, std::move(label), assisted);
        return true;
    }

    bool restore(uint32_t base, bool replay, int from)
    {
        auto &source = replay ? redo : undo;
        auto &target = replay ? undo : redo;
        if (base != revision || source.empty())
            return false;
        auto step = std::move(source.back());
        source.pop_back();
        target.push_back({board, step.label, step.edit});
        publish(std::move(step.board), from, (replay ? "Redo " : "Undo ") + step.label, false, step.edit, replay);
        return true;
    }

    void serverPacket(int id, Message message)
    {
        auto const type = message.byte();
        if (type == Hello && players[id].name.empty())
        {
            auto const data = message.word();
            auto const isAI = message.byte();
            if (!message.valid || isAI > 1 || !message.remaining || message.remaining > 36 ||
                (data != edition && !(isAI && !data)))
            {
                send(peer(id), textPacket(Problem, "Select the same Duke3D edition as the host."));
                enet_peer_disconnect_later(peer(id), 0);
                return;
            }
            auto const rawName = std::string(reinterpret_cast<char const *>(message.data), message.remaining);
            players[id] = {cleanName(rawName), bool(isAI), nextGeneration++, {}};
            Bytes greeting{Welcome, uint8_t(id)};
            number(greeting, limit);
            send(peer(id), greeting);
            for (int i = 0; i < maxEditors; ++i)
                if (!players[i].name.empty())
                    send(peer(id), playerPacket(i, players[i]));
            send(peer(id), snapshot(false));
            broadcast(playerPacket(id, players[id]), id);
            return;
        }
        if (players[id].name.empty())
            return;
        if (type == Cursor)
        {
            message.byte(); // The socket owns the participant ID.
            uint32_t generation;
            Presence presence;
            if (readCursor(message, generation, presence) && generation == players[id].generation)
            {
                clearStaleSelection(presence);
                players[id].presence = presence;
                broadcast(cursorPacket(id, players[id]), id, false);
            }
        }
        else if (type == Edit)
        {
            uint32_t const base = message.word();
            auto const flags = message.byte();
            auto const label = readLabel(message);
            Board next;
            std::string error;
            if (flags > 3 || !readBoard(message, next, limit, error) ||
                !apply(base, std::move(next), id, label, flags & 1, flags & 2))
                send(peer(id), snapshot(true));
        }
        else if (type == HistoryRequest)
        {
            uint32_t const base = message.word();
            auto const replay = message.byte();
            if (!message.finished() || replay > 1 || !restore(base, replay != 0, id))
                send(peer(id), snapshot(true));
        }
    }

    void clientPacket(Message message)
    {
        switch (message.byte())
        {
            case Welcome:
            {
                int const assigned = message.byte();
                int const newLimit = message.word();
                if (message.finished() && slot < 0 && assigned > 0 && assigned < maxEditors &&
                    (newLimit == 3840 || newLimit == 5120))
                {
                    slot = assigned;
                    limit = newLimit;
                }
                break;
            }
            case Player:
            {
                const int player = message.byte();
                const auto isAI = message.byte();
                const auto generation = message.word();
                if (message.valid && player < maxEditors && isAI <= 1 && message.remaining && message.remaining <= 36)
                    players[player] = {cleanName(std::string(reinterpret_cast<char const *>(message.data), message.remaining)),
                                       bool(isAI), generation, {}};
                break;
            }
            case Left:
            {
                int const player = message.byte();
                if (message.finished() && player < maxEditors)
                    players[player] = {};
                break;
            }
            case Cursor:
            {
                const int player = message.byte();
                uint32_t generation;
                Presence presence;
                if (player < maxEditors && !players[player].name.empty() &&
                    readCursor(message, generation, presence) && generation == players[player].generation)
                {
                    clearStaleSelection(presence);
                    players[player].presence = presence;
                }
                break;
            }
            case State:
            {
                const int by = message.byte();
                auto const flags = message.byte();
                bool const conflict = flags & 1;
                uint32_t const rev = message.word();
                auto const label = readLabel(message);
                Board next;
                std::string error;
                if (slot < 0 || flags > 15 || rev < revision || !readBoard(message, next, limit, error))
                    break;
                if (conflict)
                {
                    pending = false;
                    notice = "The host changed the same map records first. Review the live draft and retry.";
                }
                else if (by == slot)
                    pending = false;
                canUndo = flags & 2;
                canRedo = flags & 4;
                setBoard(std::move(next), rev, by == 255 ? -1 : by, label, flags & 8);
                mode = LiveMode::Joined;
                break;
            }
            case Problem:
                if (message.remaining <= 160)
                    notice.assign(reinterpret_cast<char const *>(message.data), message.remaining);
                break;
        }
    }
};

LiveSession::LiveSession() : impl(new Impl) {}
LiveSession::~LiveSession() { leave(); }

bool LiveSession::host(uint16_t port, std::string name, uint32_t edition, int limit, const Board &board)
{
    leave();
    if (!InitializeEditorNetwork())
    {
        impl->notice = "Could not initialize the editor network.";
        return false;
    }
    if (!board.validate(limit, impl->notice, true))
        return false;
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    impl->link = enet_host_create(&address, maxEditors - 1, 2, 0, 0);
    if (!impl->link)
    {
        impl->notice = "Could not listen on that UDP port.";
        return false;
    }
    ensureLimits(impl->link);
    impl->mode = LiveMode::Hosting;
    impl->slot = 0;
    impl->edition = edition;
    impl->limit = limit;
    impl->players[0] = {cleanName(name), false, impl->nextGeneration++, {}};
    impl->setBoard(board, 1, 0);
    return true;
}

bool LiveSession::join(std::string address, std::string name, uint32_t edition, bool ai)
{
    leave();
    ENetAddress endpoint{};
    if (!InitializeEditorNetwork() || !getAddress(address, endpoint))
    {
        impl->notice = "Could not resolve the editor's address.";
        return false;
    }
    impl->link = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!impl->link)
    {
        impl->notice = "Could not create an editor connection.";
        return false;
    }
    ensureLimits(impl->link);
    impl->server = enet_host_connect(impl->link, &endpoint, 2, connectCode);
    if (!impl->server)
    {
        leave();
        impl->notice = "Could not start the editor connection.";
        return false;
    }
    impl->mode = LiveMode::Connecting;
    impl->connecting = Clock::now();
    impl->edition = edition;
    impl->ai = ai;
    impl->name = cleanName(name);
    return true;
}

void LiveSession::leave()
{
    if (!impl || !impl->link)
        return;
    if (impl->hosting())
    {
        for (int i = 1; i < maxEditors; ++i)
            if (impl->peer(i)->state != ENET_PEER_STATE_DISCONNECTED)
                enet_peer_disconnect_now(impl->peer(i), 0);
    }
    else if (impl->server)
        enet_peer_disconnect_now(impl->server, 0);
    enet_host_destroy(impl->link);
    *impl = {};
}

void LiveSession::tick()
{
    if (!impl->link)
        return;
    ENetEvent event;
    for (int i = 0; i < 80 && enet_host_service(impl->link, &event, 0) > 0; ++i)
    {
        if (event.type == ENET_EVENT_TYPE_CONNECT)
        {
            enet_peer_timeout(event.peer, 10, 3000, 12000);
            if (impl->hosting() && event.data != connectCode)
                enet_peer_disconnect_now(event.peer, 0);
            else if (impl->hosting())
                impl->helloDeadline[event.peer->incomingPeerID + 1] = Clock::now() + std::chrono::seconds(8);
            else
            {
                Bytes hello{Hello};
                number(hello, impl->edition);
                hello.push_back(impl->ai);
                hello.insert(hello.end(), impl->name.begin(), impl->name.end());
                send(event.peer, hello);
            }
        }
        else if (event.type == ENET_EVENT_TYPE_RECEIVE)
        {
            Message message{event.packet->data, event.packet->dataLength};
            if (impl->hosting())
                impl->serverPacket(event.peer->incomingPeerID + 1, message);
            else
                impl->clientPacket(message);
            enet_packet_destroy(event.packet);
        }
        else if (event.type == ENET_EVENT_TYPE_DISCONNECT || event.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT)
        {
            if (impl->hosting())
            {
                int const id = event.peer->incomingPeerID + 1;
                if (!impl->players[id].name.empty())
                {
                    impl->players[id] = {};
                    impl->broadcast({Left, uint8_t(id)});
                }
            }
            else
            {
                auto notice = std::move(impl->notice);
                leave();
                impl->notice = notice.empty() ? "Disconnected from the editor host." : std::move(notice);
                return;
            }
        }
    }
    if (impl->hosting())
        for (int i = 1; i < maxEditors; ++i)
            if (impl->players[i].name.empty() && impl->peer(i)->state == ENET_PEER_STATE_CONNECTED &&
                Clock::now() > impl->helloDeadline[i])
                enet_peer_disconnect_now(impl->peer(i), 0);
    if (impl->mode == LiveMode::Connecting && Clock::now() - impl->connecting > std::chrono::seconds(8))
    {
        leave();
        impl->notice = "The editor host did not answer. Check the address and open slots.";
    }
}

bool LiveSession::submit(uint32_t base, const Board &board, std::string const &label, bool strict, bool assisted)
{
    if (impl->hosting())
    {
        if (impl->apply(base, board, 0, label, strict, assisted))
            return true;
        impl->notice = "The live draft changed first. Review it before retrying.";
        return false;
    }
    if (impl->mode != LiveMode::Joined || impl->pending || !board.validate(impl->limit, impl->notice, true))
        return false;
    Bytes message{Edit};
    number(message, base);
    message.push_back(uint8_t(strict | (assisted << 1)));
    putLabel(message, label);
    impl->pending = send(impl->server, boardPacket(std::move(message), board));
    return impl->pending;
}

bool LiveSession::useHistory(uint32_t base, bool redo)
{
    if (impl->hosting())
        return impl->restore(base, redo, 0);
    if (impl->mode != LiveMode::Joined || impl->pending || !(redo ? impl->canRedo : impl->canUndo))
        return false;
    Bytes message{HistoryRequest};
    number(message, base);
    message.push_back(redo);
    impl->pending = send(impl->server, message);
    return impl->pending;
}

void LiveSession::updatePresence(const Presence &presence)
{
    if (impl->mode != LiveMode::Hosting && impl->mode != LiveMode::Joined)
        return;
    auto const now = Clock::now();
    if (now - impl->lastCursor < std::chrono::milliseconds(55))
        return;
    auto &player = impl->players[impl->slot];
    player.presence = presence;
    if (!player.presence.revision)
        player.presence.revision = impl->revision;
    impl->clearStaleSelection(player.presence);
    auto packet = cursorPacket(impl->slot, player);
    if (packet == impl->lastPresence && now - impl->lastCursor < std::chrono::seconds(2))
        return;
    impl->lastPresence = packet;
    impl->lastCursor = now;
    if (impl->hosting())
        impl->broadcast(packet, -1, false);
    else
        send(impl->server, packet, false);
}

LiveMode LiveSession::mode() const { return impl->mode; }
int LiveSession::localSlot() const { return impl->slot; }
int LiveSession::artLimit() const { return impl->limit; }
bool LiveSession::pending() const { return impl->pending; }
bool LiveSession::undoable() const { return impl->canUndo; }
bool LiveSession::redoable() const { return impl->canRedo; }
uint32_t LiveSession::revision() const { return impl->revision; }
uint32_t LiveSession::presentation() const { return impl->presentation; }
int LiveSession::author() const { return impl->author; }
const Board &LiveSession::board() const { return impl->board; }
const Board *LiveSession::boardAtRevision(uint32_t revision) const
{
    if (impl->revision == revision)
        return &impl->board;
    auto const found = std::find_if(impl->past.begin(), impl->past.end(),
                                   [&](auto const &entry) { return entry.revision == revision; });
    return found == impl->past.end() ? nullptr : &found->board;
}
const std::array<Collaborator, maxEditors> &LiveSession::players() const { return impl->players; }
const std::deque<ChangeRecord> &LiveSession::changes() const { return impl->changes; }
std::string LiveSession::takeNotice() { auto notice = std::move(impl->notice); impl->notice.clear(); return notice; }

} // namespace DukeEditor
