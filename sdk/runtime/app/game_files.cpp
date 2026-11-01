// The game picked in the launcher, else the one in assets: its files, or a disc image (.iso) with this executable's xbe
// on it. The xbe must be the one
// this executable was recompiled from; another release's would run its own data under this code.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>

#include "app/app.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/crypto.h"
#include "kernel/disc.h"

extern const char kImageSha1[];  // the xbe recompiled into this executable, written by CMake

namespace app {

namespace fs = std::filesystem;

namespace {

bool IsDiscImage(const fs::path& path) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
    return ext == L".iso";
}

bool ReadHostFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

}  // namespace

std::string Sha1(const std::vector<uint8_t>& data) {
    alignas(8) uint8_t ctx[128];
    uint8_t digest[20];
    crypto::ShaInit(ctx);
    crypto::ShaUpdate(ctx, data.data(), data.size());
    crypto::ShaFinal(ctx, digest);
    std::string hex;
    for (uint8_t b : digest) {
        hex += "0123456789abcdef"[b >> 4];
        hex += "0123456789abcdef"[b & 15];
    }
    return hex;
}

// assets beside the exe or in any folder above it (the build output)
fs::path AssetsDir() {
    auto dir = ExeDir();
    std::error_code ec;
    for (int i = 0; i < 6; i++) {
        if (fs::is_directory(dir / "assets", ec)) return dir / "assets";
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

std::vector<fs::path> GameChoices() {
    std::vector<fs::path> games;
    std::error_code ec;
    if (const std::string& picked = Get().game; !picked.empty()) {
        fs::path path = ExeDir() / fs::path(reinterpret_cast<const char8_t*>(picked.c_str()));
        if (fs::exists(path, ec)) games.push_back(path.lexically_normal());
        else LOGW("game: %s, picked in the launcher, is gone", picked.c_str());
    }
    std::vector<fs::path> found;
    if (fs::path assets = AssetsDir(); !assets.empty()) {
        if (fs::is_regular_file(assets / IMAGE_XBE, ec)) found.push_back(assets);
        std::vector<fs::path> isos;
        for (auto& e : fs::directory_iterator(assets, ec))
            if (IsDiscImage(e.path()) && e.is_regular_file(ec)) isos.push_back(e.path());
        std::sort(isos.begin(), isos.end());
        found.insert(found.end(), isos.begin(), isos.end());
    }
    for (const fs::path& path : found)
        if (games.empty() || !fs::equivalent(games[0], path, ec)) games.push_back(path);
    return games;
}

bool FindGame(GameFiles& game) {
    // the first copy with this xbe wins, in GameChoices order; another release is kept to offer
    GameFiles other;
    std::unique_ptr<kernel::Disc> other_disc;
    fs::path other_name;
    auto consider = [&](const fs::path& name, const fs::path& dir, std::vector<uint8_t> xbe,
                        std::unique_ptr<kernel::Disc> disc) {
        std::string sha = Sha1(xbe);
        bool match = sha == kImageSha1;
        LOGR("Game: %s in %s, sha1 %s%s", IMAGE_XBE, name.string().c_str(), sha.c_str(), match ? "" : ", another release");
        if (match) {
            game = {dir, std::move(xbe)};
            kernel::MountDisc(std::move(disc));
            return true;
        }
        if (other_name.empty()) {
            other = {dir, std::move(xbe)};
            other_disc = std::move(disc);
            other_name = name;
        }
        return false;
    };

    std::vector<uint8_t> xbe;
    for (const fs::path& path : GameChoices()) {
        if (!IsDiscImage(path)) {
            if (ReadHostFile(path / IMAGE_XBE, xbe) && consider(path, path, std::move(xbe), nullptr)) return true;
            continue;
        }
        auto disc = std::make_unique<kernel::Disc>();
        std::string error;
        if (!disc->Open(path, error)) {
            LOGW("game: %s: %s", path.string().c_str(), error.c_str());
            continue;
        }
        if (!disc->Load(IMAGE_XBE, xbe)) {
            LOGW("game: %s has no %s", path.string().c_str(), IMAGE_XBE);
            continue;
        }
        if (consider(path, path.parent_path(), std::move(xbe), std::move(disc))) return true;
    }

    if (other_name.empty()) {
        MessageBoxW(nullptr, kGame.missing, kGame.short_title, MB_ICONERROR);
        return false;
    }
    std::wstring text = other_name.filename().wstring() +
                        L" is a different release of the game than this build was made from, and will most likely "
                        L"crash.\n\nStart it anyway?";
    if (MessageBoxW(nullptr, text.c_str(), kGame.short_title, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) return false;
    game = std::move(other);
    kernel::MountDisc(std::move(other_disc));
    return true;
}

}  // namespace app
