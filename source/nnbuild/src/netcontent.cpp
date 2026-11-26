#include "netcontent.h"

#include "duke3d.h"
#include "cmdline.h"
#include "crc32.h"
#include "grpscan.h"
#include "oldnet.h"

#include <array>
#include <chrono>
#include <dirent.h>
#include <thread>
#include <vector>

namespace {

constexpr int chunkSize = 1024;
constexpr int fileSendWindow = 128;
constexpr int contentProtocol = 1;
constexpr int fileHeaderSize = 24;
constexpr int idleTimeout = 20000;
constexpr char cacheDir[] = "netcache";

enum ContentKind { Pack, UserMap, LooseFile };
enum FileReply { NeedFile, HaveFile, FileSaved, FileRejected };

struct ContentFile
{
    std::string name;
    int size = 0;
    uint32_t crc = 0;
    uint32_t bundleCrc = 0;
    int bundleSize = 0;
    ContentKind kind = UserMap;
};

struct MapAsset
{
    std::string path;
    ContentFile file;
};

struct IncomingFile
{
    uint32_t id = 0;
    int offset = 0;
    uint32_t crc = 0;
    buildvfs_FILE stream = nullptr;
    ContentFile file;
    std::string path, tempPath;
};

IncomingFile incoming;
std::vector<std::string> startupPacks;
bool receivingStartup, sendingStartup;
bool startupComplete;
int32_t startupBaseCrc;
uint32_t startupKeepalive;

int replySlot = -1, fileReply = -1;
uint32_t outgoingId, lastContentProgress;

char const *basenameOf(char const *path)
{
    char const *base = path;
    for (auto p = path; *p; ++p)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    return base;
}

bool hasExtension(char const *path, char const *extension)
{
    char const *dot = Bstrrchr(basenameOf(path), '.');
    return dot && !Bstrcasecmp(dot, extension);
}

bool validName(std::string const &name)
{
    if (name.empty() || name.size() > 200 || name.front() == '.' ||
        name.back() == '.' || name.back() == ' ' || name.find("..") != std::string::npos)
        return false;

    for (unsigned char c : name)
        if (c < 32 || c == 127 || strchr("/\\:*?\"<>|", c))
            return false;

    return true;
}

buildvfs_kfd openContent(char const *path)
{
    int const previousMode = pathsearchmode;
    pathsearchmode = 1;
    auto const handle = kopen4loadfrommod(path, 0);
    pathsearchmode = previousMode;
    return handle;
}

bool identifyFile(buildvfs_kfd handle, ContentFile &file)
{
    auto const length = kfilelength(handle);
    if (length <= 0 || length > INT32_MAX)
        return false;

    file.size = static_cast<int>(length);
    file.crc = 0;
    std::array<char, 16384> block;
    for (int remaining = file.size; remaining > 0;)
    {
        int const count = min(remaining, static_cast<int>(block.size()));
        if (kread(handle, block.data(), count) != count)
            return false;
        file.crc = Bcrc32(block.data(), count, file.crc);
        remaining -= count;
    }
    return klseek(handle, 0, BSEEK_SET) == 0;
}

bool matchesFile(std::string const &path, ContentFile const &file)
{
    auto const handle = openContent(path.c_str());
    if (handle == buildvfs_kfd_invalid)
        return false;
    ContentFile local;
    bool const match = kfilelength(handle) == file.size && identifyFile(handle, local) && local.crc == file.crc;
    kclose(handle);
    return match;
}

std::string cacheDirectory(ContentFile const &file)
{
    char directory[64];
    auto const crc = file.bundleSize ? file.bundleCrc : file.crc;
    auto const size = file.bundleSize ? file.bundleSize : file.size;
    Bsnprintf(directory, sizeof(directory), "%s/%08x-%d", cacheDir, crc, size);
    return directory;
}

std::string cachePath(ContentFile const &file)
{
    return cacheDirectory(file) + "/" + file.name;
}

unsigned temporaryId;

buildvfs_FILE openCacheFile(ContentFile const &file, std::string &temp)
{
    auto const directory = cacheDirectory(file);
    buildvfs_mkdir(cacheDir, S_IRWXU);
    buildvfs_mkdir(directory.c_str(), S_IRWXU);
    // The temporary file is adjacent to the destination so a finished write
    // can be published with one rename, including across server restarts.
    char suffix[64];
    Bsnprintf(suffix, sizeof(suffix), "/.%lu-%u.part",
#ifdef _WIN32
              static_cast<unsigned long>(GetCurrentProcessId()),
#else
              static_cast<unsigned long>(getpid()),
#endif
              ++temporaryId);
    temp = directory + suffix;
    return buildvfs_fopen_write(temp.c_str());
}

bool publishCacheFile(std::string const &temp, std::string const &path)
{
    if (rename(temp.c_str(), path.c_str()) == 0)
        return true;
#ifdef _WIN32
    return MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return false;
#endif
}

bool copyToCache(buildvfs_kfd handle, ContentFile const &file)
{
    std::string const path = cachePath(file);
    if (matchesFile(path, file))
        return true;

    std::string temp;
    auto const stream = openCacheFile(file, temp);
    if (!stream)
        return false;

    std::array<char, 16384> block;
    bool ok = true;
    uint32_t crc = 0;
    for (int remaining = file.size; remaining > 0;)
    {
        int const count = min(remaining, static_cast<int>(block.size()));
        if (kread(handle, block.data(), count) != count ||
            buildvfs_fwrite(block.data(), 1, count, stream) != static_cast<size_t>(count))
        {
            ok = false;
            break;
        }
        crc = Bcrc32(block.data(), count, crc);
        remaining -= count;
    }
    ok = buildvfs_fclose(stream) == 0 && ok && crc == file.crc;
    if (ok && publishCacheFile(temp, path))
        return true;
    remove(temp.c_str());
    return false;
}

bool readMapAsset(std::string const &path, std::vector<MapAsset> &assets)
{
    ContentFile file;
    file.name = basenameOf(path.c_str());
    if (!validName(file.name))
        return false;
    auto const fd = openContent(path.c_str());
    if (fd == buildvfs_kfd_invalid)
        return false;
    bool const valid = identifyFile(fd, file);
    kclose(fd);
    if (valid)
        assets.push_back({path, std::move(file)});
    return valid;
}

bool mapExtension(std::string const &name)
{
    static char const *const allowed[] = {
        ".map", ".mhk", ".art", ".ogg", ".flac", ".mid", ".mod", ".xm", ".it", ".s3m", ".mtm"
    };
    return std::any_of(std::begin(allowed), std::end(allowed),
        [&name](const char *ext) { return hasExtension(name.c_str(), ext); });
}

std::vector<MapAsset> mapAssets(std::string const &path)
{
    std::vector<MapAsset> assets;
    if (!hasExtension(path.c_str(), ".map") || !readMapAsset(path, assets))
        return assets;

    std::string const stem = path.substr(0, path.size() - 4);
    for (auto ext : {".mhk", ".ogg", ".flac", ".mid", ".mod", ".xm", ".it", ".s3m", ".mtm"})
        readMapAsset(stem + ext, assets);

    for (int art = 0; art < 100; ++art)
    {
        char suffix[12];
        Bsnprintf(suffix, sizeof(suffix), "_%02d.art", art);
        if (!readMapAsset(stem + suffix, assets))
            break;
    }
    return assets;
}

bool findLocalFile(char const *directory, ContentFile const &file, std::string &path, int depth)
{
    if (depth > 6)
        return false;
    auto const dir = opendir(directory);
    if (!dir)
        return false;

    bool found = false;
    while (auto const entry = readdir(dir))
    {
        if (entry->d_name[0] == '.')
            continue;
        std::string const candidate = std::string(directory) + "/" + entry->d_name;
        if (candidate.size() >= BMAX_PATH)
            continue;
        if (!Bstrcasecmp(entry->d_name, file.name.c_str()) && matchesFile(candidate, file))
        {
            found = Net_ResolveContentPath(candidate.c_str(), path);
            if (found)
                break;
        }

#ifndef _WIN32
        struct stat info;
        if (lstat(candidate.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
            continue;
#else
        auto const attrs = GetFileAttributesA(candidate.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
            (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
#endif
        found = findLocalFile(candidate.c_str(), file, path, depth + 1);
        if (found)
            break;
    }
    closedir(dir);
    return found;
}

void sendReply(int slot, uint32_t id, FileReply reply)
{
    unsigned char packet[6] = { PACKET_TYPE_FILE_REPLY };
    B_BUF32(packet + 1, id);
    packet[5] = reply;
    mmulti_sendpacket(slot, packet, sizeof(packet));
}

void clearIncoming(void)
{
    if (incoming.stream)
        buildvfs_fclose(incoming.stream);
    if (!incoming.tempPath.empty())
        remove(incoming.tempPath.c_str());
    incoming = {};
}

void rememberPack(ContentFile const &file, std::string const &path)
{
    if (file.kind == Pack)
        startupPacks.emplace_back(path);
}

bool readMapKey(const char *map, unsigned &crc, int &size)
{
    int prefix = 0;
    return sscanf(map, "/netcache/%8x-%d/%n", &crc, &size, &prefix) == 2 && prefix && size > 0;
}

void receiveOffer(int slot, const char *packet, int length)
{
    if (length <= fileHeaderSize || length > fileHeaderSize + 200)
        return;
    clearIncoming();

    uint32_t const id = B_UNBUF32(packet + 1);
    ContentFile file;
    file.kind = static_cast<ContentKind>(static_cast<uint8_t>(packet[5]));
    file.size = static_cast<int32_t>(B_UNBUF32(packet + 6));
    file.crc = B_UNBUF32(packet + 10);
    unsigned const index = static_cast<uint8_t>(packet[14]);
    unsigned const total = static_cast<uint8_t>(packet[15]);
    file.bundleCrc = B_UNBUF32(packet + 16);
    file.bundleSize = static_cast<int32_t>(B_UNBUF32(packet + 20));
    file.name.assign(packet + fileHeaderSize, length - fileHeaderSize);

    if (file.size <= 0 || index == 0 || index > total || !validName(file.name) ||
        (file.kind == Pack && (!receivingStartup || startupPacks.size() >= MAXNETMODFILES ||
                              !Net_ValidPackName(file.name.c_str()))) ||
        (file.kind == UserMap && (receivingStartup || file.bundleSize <= 0 || !mapExtension(file.name))) ||
        (file.kind != UserMap && file.bundleSize != 0) ||
        (file.kind == LooseFile && receivingStartup) ||
        (file.kind != Pack && file.kind != UserMap && file.kind != LooseFile))
    {
        sendReply(slot, id, FileRejected);
        return;
    }

    std::string path = cachePath(file);
    std::string existing;
    bool have = matchesFile(path, file);
    if (!have && file.kind == Pack && Net_ResolveContentPath(file.name.c_str(), existing))
        have = matchesFile(existing, file);
    if (!have && file.kind == Pack)
        have = findLocalFile(".", file, existing, 0);
    if (!have && file.kind == UserMap &&
        (matchesFile(file.name, file) || findLocalFile(".", file, existing, 0)))
    {
        auto const local = openContent(existing.empty() ? file.name.c_str() : existing.c_str());
        if (local != buildvfs_kfd_invalid)
        {
            have = copyToCache(local, file);
            kclose(local);
        }
        existing.clear();
    }

    if (have)
    {
        rememberPack(file, existing.empty() ? path : existing);
        LOG_F(INFO, "Using local %s (file %u of %u)", file.name.c_str(), index, total);
        sendReply(slot, id, HaveFile);
        return;
    }

    incoming.path = path;
    incoming.stream = openCacheFile(file, incoming.tempPath);
    if (!incoming.stream)
    {
        LOG_F(ERROR, "Could not open the download cache for %s", file.name.c_str());
        clearIncoming();
        sendReply(slot, id, FileRejected);
        return;
    }

    incoming.id = id;
    incoming.file = std::move(file);
    LOG_F(INFO, "Downloading %s (%d bytes, file %u of %u)",
          incoming.file.name.c_str(), incoming.file.size, index, total);
    Bsnprintf(g_lateJoinStatus, sizeof(g_lateJoinStatus), "Downloading %.30s (%u of %u)",
              incoming.file.name.c_str(), index, total);
    startwin_settitle(g_lateJoinStatus);
    g_lateJoinPercent = 0;
    sendReply(slot, id, NeedFile);
}

void receiveChunk(int slot, char const *packet, int length)
{
    if (length <= 9 || length > 9 + chunkSize || !incoming.stream)
        return;

    uint32_t const id = B_UNBUF32(packet + 1);
    uint32_t const offset = B_UNBUF32(packet + 5);
    int const bytes = length - 9;
    if (id != incoming.id)
        return;

    if (offset != static_cast<uint32_t>(incoming.offset) || bytes > incoming.file.size - incoming.offset ||
        buildvfs_fwrite(packet + 9, 1, bytes, incoming.stream) != static_cast<size_t>(bytes))
    {
        clearIncoming();
        sendReply(slot, id, FileRejected);
        return;
    }

    incoming.offset += bytes;
    incoming.crc = Bcrc32(packet + 9, bytes, incoming.crc);
    g_lateJoinPercent = static_cast<int>(100LL * incoming.offset / incoming.file.size);
    if (incoming.offset < incoming.file.size)
        return;

    bool const closed = buildvfs_fclose(incoming.stream) == 0;
    incoming.stream = nullptr;
    bool const saved = closed && incoming.crc == incoming.file.crc &&
                       publishCacheFile(incoming.tempPath, incoming.path);
    if (saved)
    {
        rememberPack(incoming.file, incoming.path);
        LOG_F(INFO, "Received %s (%d bytes)", incoming.file.name.c_str(), incoming.file.size);
    }
    else
        LOG_F(ERROR, "Could not verify and save %s", incoming.file.name.c_str());
    clearIncoming();
    sendReply(slot, id, saved ? FileSaved : FileRejected);
}

int waitForReply(void)
{
    auto const start = timerGetTicks();
    while (fileReply == -1 && mmulti_slotconnected(replySlot) && timerGetTicks() - start < idleTimeout)
    {
        Net_KeepWaitingPlayersAlive(replySlot);
        handleevents();
        Net_ParsePackets();
        if (fileReply == -1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return fileReply;
}

bool sendFile(int slot, char const *path, ContentKind kind, int index, int total,
              uint32_t bundleCrc = 0, int bundleSize = 0)
{
    if (!mmulti_slotconnected(slot))
        return false;
    ContentFile file;
    file.kind = kind;
    file.name = basenameOf(path);
    if (!validName(file.name))
        return false;

    auto const fd = openContent(path);
    if (fd == buildvfs_kfd_invalid)
        return false;
    if (!identifyFile(fd, file))
    {
        kclose(fd);
        return false;
    }
    if (G_IsBaseGrp(file.crc, file.size))
    {
        LOG_F(ERROR, "%s is base game data; each player needs their own copy", path);
        kclose(fd);
        return false;
    }

    uint32_t const id = ++outgoingId;
    unsigned char offer[fileHeaderSize + 200] = { PACKET_TYPE_FILE_HEADER };
    B_BUF32(offer + 1, id);
    offer[5] = kind;
    B_BUF32(offer + 6, file.size);
    B_BUF32(offer + 10, file.crc);
    offer[14] = index;
    offer[15] = total;
    B_BUF32(offer + 16, bundleCrc);
    B_BUF32(offer + 20, bundleSize);
    Bmemcpy(offer + fileHeaderSize, file.name.data(), file.name.size());
    replySlot = slot;
    fileReply = -1;
    mmulti_sendpacket(slot, offer, fileHeaderSize + file.name.size());
    int const answer = waitForReply();
    bool ok = answer == HaveFile;

    if (answer == NeedFile)
    {
        LOG_F(INFO, "Sending %s (%d bytes) to player %d", file.name.c_str(), file.size, slot);
        unsigned char chunk[9 + chunkSize] = { PACKET_TYPE_FILE_CHUNK };
        B_BUF32(chunk + 1, id);
        fileReply = -1;
        ok = true;
        for (int offset = 0; offset < file.size;)
        {
            Net_KeepWaitingPlayersAlive(slot);
            int const count = min(chunkSize, file.size - offset);
            if (!Net_DrainReliableWindow(fileSendWindow) || fileReply == FileRejected || !mmulti_slotconnected(slot) ||
                kread(fd, chunk + 9, count) != count)
            {
                ok = false;
                break;
            }
            B_BUF32(chunk + 5, offset);
            mmulti_sendpacket(slot, chunk, 9 + count);
            offset += count;
        }
        ok = ok && waitForReply() == FileSaved;
    }

    kclose(fd);
    replySlot = -1;
    if (!ok)
        LOG_F(WARNING, "Content transfer to player %d failed for %s", slot, file.name.c_str());
    return ok;
}

} // namespace

bool Net_ValidPackName(const char *name)
{
    return hasExtension(name, ".grp") || hasExtension(name, ".zip") ||
           hasExtension(name, ".pk3") || hasExtension(name, ".pk4") || hasExtension(name, ".ssi");
}

bool Net_ResolveContentPath(const char *name, std::string &path)
{
    char *where = nullptr;
    int const previousMode = pathsearchmode;
    pathsearchmode = 1;
    int const result = findfrompath(name, &where);
    pathsearchmode = previousMode;
    if (result != 0)
        return false;

    path = where;
    Xfree(where);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path[0] != '/' && (path.size() < 2 || path[1] != ':'))
    {
        char cwd[BMAX_PATH];
        if (!buildvfs_getcwd(cwd, sizeof(cwd)))
            return false;
        path = std::string(cwd) + "/" + path;
        std::replace(path.begin(), path.end(), '\\', '/');
    }
    if (path.size() >= BMAX_PATH || buildvfs_isdir(path.c_str()))
        return false;

    char full[BMAX_PATH];
    Bstrcpy(full, path.c_str());
    Bcorrectfilename(full + (full[1] == ':' ? 2 : 0), 0);
    path = full;
    return true;
}

bool Net_ResolvePack(const char *name, std::string &path)
{
    if (!Net_ValidPackName(name) || !validName(basenameOf(name)) || !Net_ResolveContentPath(name, path))
        return false;

    auto const fd = openContent(path.c_str());
    if (fd == buildvfs_kfd_invalid)
        return false;
    ContentFile file;
    file.kind = Pack;
    file.name = basenameOf(path.c_str());
    bool const ok = identifyFile(fd, file) && !G_IsBaseGrp(file.crc, file.size) && copyToCache(fd, file);
    kclose(fd);
    return ok && Net_ResolveContentPath(cachePath(file).c_str(), path);
}

bool Net_StageUserMap(const char *path, std::string &staged)
{
    auto assets = mapAssets(path);
    if (assets.empty())
        return false;

    uint32_t crc = 0;
    for (auto const &asset : assets)
    {
        unsigned char identity[8];
        B_BUF32(identity, asset.file.crc);
        B_BUF32(identity + 4, asset.file.size);
        crc = Bcrc32(asset.file.name.c_str(), asset.file.name.size() + 1, crc);
        crc = Bcrc32(identity, sizeof(identity), crc);
    }
    int const size = assets.front().file.size;
    for (auto &asset : assets)
    {
        asset.file.bundleCrc = crc;
        asset.file.bundleSize = size;
        auto const fd = openContent(asset.path.c_str());
        if (fd == buildvfs_kfd_invalid)
            return false;
        bool const copied = copyToCache(fd, asset.file);
        kclose(fd);
        if (!copied)
            return false;
    }
    staged = "/" + cachePath(assets.front().file);
    return true;
}

bool Net_HandleContentPacket(int slot, const char *packet, int length)
{
    bool const host = myconnectindex == connecthead;
    switch (packet[0])
    {
        case PACKET_TYPE_FILE_HEADER:
            if (!host)
                receiveOffer(slot, packet, length);
            break;
        case PACKET_TYPE_FILE_CHUNK:
            if (!host)
                receiveChunk(slot, packet, length);
            break;
        case PACKET_TYPE_FILE_REPLY:
            if (host && slot == replySlot && length == 6 && B_UNBUF32(packet + 1) == outgoingId &&
                static_cast<unsigned char>(packet[5]) <= FileRejected)
                fileReply = static_cast<unsigned char>(packet[5]);
            break;
        case PACKET_TYPE_CONTENT_READY:
            if (!host && receivingStartup && length == 6 && packet[1] == contentProtocol)
            {
                startupBaseCrc = static_cast<int32_t>(B_UNBUF32(packet + 2));
                startupComplete = true;
            }
            break;
        default:
            return false;
    }

    if (!host)
        lastContentProgress = timerGetTicks();
    return true;
}

static bool sendStartupPacks(int slot)
{
    for (int i = 0; i < g_netModFileCount; ++i)
        if (!sendFile(slot, g_netModFiles[i], Pack, i + 1, g_netModFileCount))
            return false;

    return true;
}

static void sendContentReady(int slot)
{
    unsigned char ready[6] = { PACKET_TYPE_CONTENT_READY, contentProtocol };
    B_BUF32(ready + 2, g_selectedGrp ? g_selectedGrp->type->crcval : 0);
    mmulti_sendpacket(slot, ready, sizeof(ready));
}

void Net_KeepWaitingPlayersAlive(int activeSlot)
{
    auto const now = timerGetTicks();
    if (now - startupKeepalive < 3000)
        return;
    startupKeepalive = now;

    // Startup transfers run one peer at a time. The remaining peers may
    // already have handshaken and be waiting to receive their packs.
    unsigned char packet[] = { PACKET_TYPE_CONTENT_READY };
    for (int slot = 1; slot < numplayers; ++slot)
        if (slot != activeSlot && mmulti_slotconnected(slot) && (sendingStartup || !g_player[slot].connected))
            mmulti_sendpacket(slot, packet, sizeof(packet));
}

bool Net_SendStartupContent(int slot)
{
    if (!sendStartupPacks(slot))
        return false;
    sendContentReady(slot);
    return true;
}

bool Net_ContentOnly(void)
{
    return sendingStartup;
}

bool Net_BootstrapContent(void)
{
    if (numplayers < 2)
        return true;
    if (myconnectindex == connecthead)
    {
        sendingStartup = true;
        for (int i = 1; i < numplayers; ++i)
        {
            if (!g_player[i].connected)
                continue;
            if (!sendStartupPacks(i))
            {
                mmulti_dropslot(i);
                g_player[i].connected = 0;
            }
        }
        sendingStartup = false;
        Net_RebuildConnectList();
        for (int i = 1; i < numplayers; ++i)
            if (g_player[i].connected)
                sendContentReady(i);
        return true;
    }

    receivingStartup = true;
    lastContentProgress = timerGetTicks();
    char packet[2048];
    int slot, length;
    while (!startupComplete && timerGetTicks() - lastContentProgress < idleTimeout)
    {
        handleevents();
        if (quitevent)
            break;
        if ((length = mmulti_getpacket_sized(&slot, reinterpret_cast<unsigned char *>(packet), sizeof(packet))) > 0 &&
            slot == connecthead)
            Net_HandleContentPacket(slot, packet, length);
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    receivingStartup = false;
    startwin_settitle(APPNAME);
    if (!startupComplete)
    {
        clearIncoming();
        LOG_F(ERROR, "The server did not finish content negotiation. Check the connection and game version.");
        return false;
    }

    if (startupBaseCrc && (!g_selectedGrp || g_selectedGrp->type->crcval != startupBaseCrc))
    {
        g_selectedGrp = FindGroup(startupBaseCrc);
        if (!g_selectedGrp)
        {
            LOG_F(ERROR, "The server uses different base game data (GRP CRC %08x). Select the matching GRP.",
                  static_cast<unsigned>(startupBaseCrc));
            return false;
        }
    }

    if (!startupPacks.empty())
    {
        g_noAutoLoad = 1;
        clearScriptNamePtr();
        g_scriptNamePtr = nullptr;
        clearDefNamePtr();
        g_defNamePtr = nullptr;
    }
    G_ReplaceCommandGroups(startupPacks);
    return true;
}

bool Net_SendGameFiles(int slot)
{
    return Net_SendMap(boardfilename, slot);
}

bool Net_IsCachedMap(const char *map)
{
    unsigned crc;
    int size;
    return readMapKey(map, crc, size);
}

bool Net_SendMap(const char *map, int slot)
{
    if (myconnectindex != connecthead || !map[0])
        return true;

    unsigned bundle;
    int size;
    if (!readMapKey(map, bundle, size))
        return false;
    auto const assets = mapAssets(map);
    if (assets.empty())
        return false;
    for (int i = 1; i < numplayers; ++i)
    {
        if (!(slot < 0 ? g_player[i].connected : slot == i))
            continue;
        for (size_t index = 0; index < assets.size(); ++index)
        {
            if (!sendFile(i, assets[index].path.c_str(), UserMap, index + 1, assets.size(), bundle, size))
                return false;
        }
    }
    return true;
}

bool Net_SendLooseFile(const char *path)
{
    if (myconnectindex != connecthead || numplayers < 2)
        return false;
    for (int i = 1; i < numplayers; ++i)
        if (g_player[i].connected && !sendFile(i, path, LooseFile, 1, 1))
            return false;
    return true;
}
