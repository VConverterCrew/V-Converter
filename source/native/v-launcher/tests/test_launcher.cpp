#include "vlauncher/VLauncher.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

static std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

#if defined(_WIN32)
static const char* gameName = "Funkin.exe";
#else
static const char* gameName = "Funkin";
#endif

static fs::path makeGame(const fs::path& folder, const fs::path& marker) {
    fs::path game = folder / gameName;
#if defined(_WIN32)
    writeFile(game, "MZ");
    (void)marker;
#else
    writeFile(game, "#!/bin/sh\npwd > \"" + marker.string() + "\"\n");
    fs::permissions(game, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
#endif
    return game;
}

static void testResolve(const fs::path& root) {
    char out[4096];
    CHECK(vlauncher_resolve((root / "nothing").string().c_str(), out, sizeof out) == VLAUNCHER_NOT_FOUND);
    fs::path install = root / "funkin-linux-64bit";
    fs::path game = makeGame(install, root / "marker.txt");
    CHECK(vlauncher_resolve(game.string().c_str(), out, sizeof out) == VLAUNCHER_OK);
    CHECK(fs::equivalent(out, game));
    CHECK(vlauncher_resolve(install.string().c_str(), out, sizeof out) == VLAUNCHER_OK);
    CHECK(fs::equivalent(out, game));
    CHECK(vlauncher_resolve(root.string().c_str(), out, sizeof out) == VLAUNCHER_OK);
    CHECK(fs::equivalent(out, game));
    CHECK(vlauncher_resolve(root.string().c_str(), out, 4) == VLAUNCHER_TOO_SMALL);

    std::string missing = (root / "missing").string();
    std::string top = root.string();
    const char* roots[] = {missing.c_str(), "", top.c_str()};
    CHECK(vlauncher_search(roots, 3, out, sizeof out) == VLAUNCHER_OK);
    CHECK(fs::equivalent(out, game));
    CHECK(vlauncher_search(roots, 2, out, sizeof out) == VLAUNCHER_NOT_FOUND);
}

static void testInstall(const fs::path& root) {
    fs::path game = root / "funkin-linux-64bit" / gameName;
    char mods[4096];
    CHECK(vlauncher_mods_folder(game.string().c_str(), mods, sizeof mods) == VLAUNCHER_OK);
    CHECK(fs::is_directory(mods));
    CHECK(fs::equivalent(mods, game.parent_path() / "mods"));

    fs::path source = root / "converted" / "My Mod";
    writeFile(source / "_polymod_metadata.json", "{\"title\":\"My Mod\"}");
    writeFile(source / "data" / "songs" / "bopeebo" / "bopeebo-chart.json", "{}");
    writeFile(source / "images" / "icons" / "icon-dad.png", "png");
    int32_t files = 0;
    CHECK(vlauncher_install_folder(source.string().c_str(), mods, "My Mod", &files) == VLAUNCHER_OK);
    CHECK(files == 3);
    CHECK(readFile(fs::path(mods) / "My Mod" / "_polymod_metadata.json") == "{\"title\":\"My Mod\"}");

    writeFile(source / "_polymod_metadata.json", "{\"title\":\"Again\"}");
    CHECK(vlauncher_install_folder(source.string().c_str(), mods, "My Mod", &files) == VLAUNCHER_OK);
    CHECK(readFile(fs::path(mods) / "My Mod" / "_polymod_metadata.json") == "{\"title\":\"Again\"}");

    fs::path installed = fs::path(mods) / "My Mod";
    CHECK(vlauncher_install_folder(installed.string().c_str(), mods, "My Mod", &files) == VLAUNCHER_OK);
    CHECK(files == 3);

    CHECK(vlauncher_install_folder(source.string().c_str(), mods, "../../Bad:Name", &files) == VLAUNCHER_OK);
    CHECK(fs::is_directory(fs::path(mods) / ".._.._Bad_Name") || fs::is_directory(fs::path(mods) / "_.._Bad_Name"));
    CHECK(vlauncher_install_folder(source.string().c_str(), mods, "...", &files) == VLAUNCHER_INVALID);
    CHECK(vlauncher_install_folder((root / "nope").string().c_str(), mods, "x", &files) == VLAUNCHER_NOT_FOUND);
    CHECK(vlauncher_install_folder(root.string().c_str(), (root / "converted").string().c_str(), "inside", &files) == VLAUNCHER_INVALID);
}

static void testLaunch(const fs::path& root) {
    CHECK(vlauncher_launch((root / "missing" / gameName).string().c_str()) == VLAUNCHER_NOT_FOUND);
#if !defined(_WIN32)
    fs::path game = root / "funkin-linux-64bit" / gameName;
    fs::path marker = root / "marker.txt";
    CHECK(vlauncher_launch(game.string().c_str()) == VLAUNCHER_OK);
    for (int i = 0; i < 100 && readFile(marker).empty(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::string where = readFile(marker);
    while (!where.empty() && (where.back() == '\n' || where.back() == '\r')) where.pop_back();
    CHECK(!where.empty());
    if (!where.empty()) CHECK(fs::equivalent(where, game.parent_path()));

    fs::path broken = root / "broken" / gameName;
    writeFile(broken, "\x7f" "ELF-not-really");
    fs::permissions(broken, fs::perms::owner_all);
    CHECK(vlauncher_launch(broken.string().c_str()) == VLAUNCHER_FAILED);
#endif
}

static void testPsych(const fs::path& root) {
    char out[4096];
    fs::path folder = root / "psych" / "PsychEngine-1.0.4";
#if defined(_WIN32)
    fs::path game = folder / "PsychEngine.exe";
    writeFile(game, "MZ");
#else
    fs::path game = folder / "PsychEngine";
    writeFile(game, "#!/bin/sh\n");
    fs::permissions(game, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
#endif
    std::string top = (root / "psych").string();
    CHECK(vlauncher_resolve_game(VLAUNCHER_GAME_PSYCH, top.c_str(), out, sizeof out) == VLAUNCHER_OK);
    CHECK(fs::equivalent(out, game));
    CHECK(vlauncher_resolve_game(VLAUNCHER_GAME_FUNKIN, top.c_str(), out, sizeof out) == VLAUNCHER_NOT_FOUND);
    std::string funkinOnly = (root / "funkin-linux-64bit").string();
    CHECK(vlauncher_resolve_game(VLAUNCHER_GAME_PSYCH, funkinOnly.c_str(), out, sizeof out) == VLAUNCHER_NOT_FOUND);
    CHECK(vlauncher_resolve_game(7, top.c_str(), out, sizeof out) == VLAUNCHER_INVALID);
    const char* roots[] = {funkinOnly.c_str(), top.c_str()};
    CHECK(vlauncher_search_game(VLAUNCHER_GAME_PSYCH, roots, 2, out, sizeof out) == VLAUNCHER_OK);
    CHECK(fs::equivalent(out, game));

    char mods[4096];
    CHECK(vlauncher_mods_folder(game.string().c_str(), mods, sizeof mods) == VLAUNCHER_OK);
    writeFile(folder / "modsList.txt", "Other|1\r\nMy Mod|0\nMy Mod|1\n");
    CHECK(vlauncher_enable_psych_mod(mods, "My Mod") == VLAUNCHER_OK);
    CHECK(readFile(folder / "modsList.txt") == "Other|1\nMy Mod|1");
    CHECK(vlauncher_enable_psych_mod(mods, "New: Mod") == VLAUNCHER_OK);
    CHECK(readFile(folder / "modsList.txt") == "New_ Mod|1\nOther|1\nMy Mod|1");
    CHECK(vlauncher_enable_psych_mod(mods, "..") == VLAUNCHER_INVALID);
}

static fs::path makeLuaSlice(const fs::path& folder) {
#if defined(_WIN32)
    fs::path game = folder / "LuaSlice.exe";
    writeFile(game, "MZ");
#else
    fs::path game = folder / "LuaSlice";
    writeFile(game, "#!/bin/sh\n");
    fs::permissions(game, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
#endif
    return game;
}

static void testLuaSlice(const fs::path& root) {
    char out[4096];
    fs::path first = makeLuaSlice(root / "games-a" / "LuaSlice-v0.1.0");
    fs::path second = makeLuaSlice(root / "games-b" / "luaslice");
    std::string a = (root / "games-a").string();
    std::string b = (root / "games-b").string();
    std::string empty = (root / "games-empty").string();
    fs::create_directories(empty);
    CHECK(vlauncher_resolve_game(VLAUNCHER_GAME_LUASLICE, a.c_str(), out, sizeof out) == VLAUNCHER_OK);
    CHECK(fs::equivalent(out, first));
    CHECK(vlauncher_resolve_game(VLAUNCHER_GAME_PSYCH, a.c_str(), out, sizeof out) == VLAUNCHER_NOT_FOUND);
    const char* roots[] = {empty.c_str(), b.c_str(), a.c_str()};
    for (int i = 0; i < 20; i++) {
        CHECK(vlauncher_search_game(VLAUNCHER_GAME_LUASLICE, roots, 3, out, sizeof out) == VLAUNCHER_OK);
        CHECK(fs::equivalent(out, second));
    }
    const char* none[] = {empty.c_str()};
    CHECK(vlauncher_search_game(VLAUNCHER_GAME_LUASLICE, none, 1, out, sizeof out) == VLAUNCHER_NOT_FOUND);
    CHECK(vlauncher_search_game(9, roots, 3, out, sizeof out) == VLAUNCHER_INVALID);

    char mods[4096];
    CHECK(vlauncher_mods_folder(first.string().c_str(), mods, sizeof mods) == VLAUNCHER_OK);
    CHECK(fs::equivalent(mods, first.parent_path() / "mods"));
    fs::path source = root / "lua-mod";
    for (int i = 0; i < 64; i++) writeFile(source / "images" / ("sprite" + std::to_string(i) + ".png"), std::string(100 + i, 'x'));
    writeFile(source / "scripts" / "global.luap", "function onCreate() end");
    int32_t files = 0, copied = 0;
    CHECK(vlauncher_install_folder_ex(source.string().c_str(), mods, "Lua Mod", &files, &copied) == VLAUNCHER_OK);
    CHECK(files == 65);
    CHECK(copied == 65);
    CHECK(readFile(fs::path(mods) / "Lua Mod" / "images" / "sprite63.png") == std::string(163, 'x'));
    CHECK(vlauncher_install_folder_ex(source.string().c_str(), mods, "Lua Mod", &files, &copied) == VLAUNCHER_OK);
    CHECK(files == 65);
    CHECK(copied == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    writeFile(source / "scripts" / "global.luap", "function onCreate() debugPrint(1) end");
    CHECK(vlauncher_install_folder_ex(source.string().c_str(), mods, "Lua Mod", &files, &copied) == VLAUNCHER_OK);
    CHECK(copied == 1);
    CHECK(readFile(fs::path(mods) / "Lua Mod" / "scripts" / "global.luap") == "function onCreate() debugPrint(1) end");
}

int main() {
    fs::path root = fs::temp_directory_path() / ("vlauncher-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    root = fs::canonical(root);
    CHECK(std::string(vlauncher_version()) == VLAUNCHER_VERSION_STRING);
    CHECK(vlauncher_resolve(nullptr, nullptr, 0) == VLAUNCHER_INVALID);
    testResolve(root);
    testInstall(root);
    testLaunch(root);
    testPsych(root);
    testLuaSlice(root);
    std::error_code error;
    fs::remove_all(root, error);
    if (failures == 0) std::printf("v-launcher: all tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
