#include "vbanana/VBanana.h"

#include "Json.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static vbanana::Json take(char* raw) {
    std::string text = raw ? raw : "";
    vbanana_free(raw);
    bool ok = false;
    vbanana::Json json = vbanana::Json::parse(text, ok);
    if (!ok) std::fprintf(stderr, "bad json: %s\n", text.c_str());
    return json;
}

static void testRequests() {
    vbanana::Json r = take(vbanana_request("{\"kind\":\"feed\",\"sort\":\"new\",\"page\":2}"));
    CHECK(r["url"].text() == "https://gamebanana.com/apiv11/Game/8694/Subfeed?_nPage=2&_sSort=new&_csvModelInclusions=Mod");
    CHECK(r["headers"]["Accept"].text() == "application/json");
    CHECK(r["cacheSeconds"].asNumber() == 300);
    r = take(vbanana_request("{\"kind\":\"search\",\"query\":\" vs dave & bambi \"}"));
    CHECK(r["url"].text() == "https://gamebanana.com/apiv11/Util/Search/Results?_sModelName=Mod&_sOrder=best_match&_idGameRow=8694&_sSearchString=vs%20dave%20%26%20bambi&_nPage=1");
    r = take(vbanana_request("{\"kind\":\"search\",\"query\":\"a\"}"));
    CHECK(!r["error"].isNull());
    r = take(vbanana_request("{\"kind\":\"updates\",\"id\":516489,\"perPage\":5}"));
    CHECK(r["url"].text() == "https://gamebanana.com/apiv11/Mod/516489/Updates?_nPage=1&_nPerpage=5");
    r = take(vbanana_request("{\"kind\":\"mod\",\"id\":516489}"));
    CHECK(r["url"].text() == "https://gamebanana.com/apiv11/Mod/516489/ProfilePage");
    r = take(vbanana_request("not json"));
    CHECK(!r["error"].isNull());
}

static void testLinks() {
    CHECK(take(vbanana_link("https://gamebanana.com/mods/516489"))["id"].asNumber() == 516489);
    CHECK(take(vbanana_link("gamebanana.com/mods/download/516489#FileInfo_1"))["kind"].text() == "mod");
    vbanana::Json file = take(vbanana_link("https://gamebanana.com/dl/1897608"));
    CHECK(file["kind"].text() == "file");
    CHECK(file["id"].asNumber() == 1897608);
    CHECK(take(vbanana_link("https://gamebanana.com/members/1954873"))["kind"].text() == "member");
    CHECK(take(vbanana_link("  42  "))["id"].asNumber() == 42);
    CHECK(take(vbanana_link("https://example.com/mods/1"))["kind"].text() == "none");
    CHECK(take(vbanana_link("https://gamebanana.com/mods/cats/43780"))["kind"].text() == "none");
}

static void testParse() {
    const char* page = R"J({"_aMetadata":{"_nRecordCount":164534,"_bIsComplete":false,"_nPerpage":15},"_aRecords":[
        {"_idRow":517012,"_sModelName":"Mod","_sName":"Time Bar [V-Slice Mod]","_bHasFiles":true,"_sVersion":"1.2","_tsDateUpdated":1795212642,
         "_aPreviewMedia":{"_aImages":[{"_sBaseUrl":"https://images.gamebanana.com/img/ss/mods","_sFile":"a.jpg","_sFile220":"220-90_a.jpg"}]},
         "_aSubmitter":{"_idRow":1,"_sName":"someone"},"_aRootCategory":{"_sName":"Mod Folders"},"_nLikeCount":132,"_nViewCount":117354},
        {"_idRow":5,"_sModelName":"Wip","_sName":"Not a mod"},
        {"_idRow":734305,"_sModelName":"Mod","_sName":"Other game","_aGame":{"_idRow":6137}},
        {"_idRow":"516489","_sModelName":"Mod","_sName":"Combos Reimplemented [Psych Engine 0.7.3]","_nLikeCount":"108"}
    ]})J";
    vbanana::Json p = take(vbanana_parse("page", page, "{}"));
    CHECK(p["mods"].items().size() == 2);
    CHECK(p["total"].asNumber() == 164534);
    CHECK(!p["complete"].asBool(true));
    const vbanana::Json& first = p["mods"].items()[0];
    CHECK(first["thumbnail"].text() == "https://images.gamebanana.com/img/ss/mods/220-90_a.jpg");
    CHECK(first["engine"].text() == "V-Slice");
    CHECK(first["version"].text() == "1.2");
    CHECK(first["updated"].asNumber() == 1795212642);
    CHECK(p["mods"].items()[1]["id"].asNumber() == 516489);
    CHECK(p["mods"].items()[1]["likes"].asNumber() == 108);
    CHECK(p["mods"].items()[1]["engine"].text() == "Psych Engine");

    const char* mod = R"J({"_idRow":516489,"_sModelName":"Mod","_sName":"Combos Reimplemented","_aGame":{"_idRow":8694},
        "_aSuperCategory":{"_sName":"Base Game Mod Folders"},"_aCategory":{"_sName":"Other/Misc (Base)"},"_nDownloadCount":226747,
        "_sText":"<p>A mod for the <b>FNF V-Slice</b> Engine&amp; more.</p><ul><li>Combos</li><li>Ratings</li></ul>",
        "_sDescription":"Combos Reimplemented!","_sLicense":"CC BY 4.0<a href='x'>, MIT</a>","_nUpdatesCount":20,
        "_aRequirements":[["Friday Night Funkin 0.8.0+","https://ninja-muffin24.itch.io/funkin"]],
        "_aCredits":[{"_sGroupName":"Mod Credits","_aAuthors":[{"_sRole":"Creator","_sName":"JugieNoob"}]}],
        "_aAlternateFileSources":[{"url":"https://github.com/x/releases","description":"GitHub Downloads"}],
        "_aArchivedFiles":[{"_sFile":"old.zip"}],
        "_aFiles":[
          {"_idRow":1599940,"_sFile":"combos_143.zip","_nFilesize":1070508,"_sDescription":"Haptics Fix","_tsDateAdded":1768083392,"_sAnalysisResult":"ok","_sAvResult":"clean"},
          {"_idRow":1897608,"_sFile":"combos_144.zip","_nFilesize":1070218,"_tsDateAdded":1795212480,"_sAnalysisResult":"ok","_sAvResult":"clean","_sVersion":"1.4.4"},
          {"_idRow":1897609,"_sFile":"combos_windows_build.zip","_nFilesize":90000000,"_tsDateAdded":1795212999,"_sAnalysisResult":"ok","_sAvResult":"clean"},
          {"_idRow":1900000,"_sFile":"readme.txt","_nFilesize":10,"_tsDateAdded":1795212999,"_sAnalysisResult":"ok"},
          {"_idRow":1900001,"_sFile":"suspicious.zip","_nFilesize":10,"_tsDateAdded":1795213999,"_sAnalysisResult":"suspicious","_sAvResult":"infected"}
        ]})J";
    vbanana::Json d = take(vbanana_parse("mod", mod, "{}"));
    CHECK(d["ok"].asBool());
    CHECK(d["engine"]["name"].text() == "V-Slice");
    CHECK(d["engine"]["version"].text() == "0.8.0");
    CHECK(d["engine"]["confidence"].asNumber() >= 60);
    CHECK(!d["engine"]["reasons"].items().empty());
    CHECK(d["best"].asNumber() == 1897608);
    CHECK(d["files"].items().size() == 5);
    CHECK(d["files"].items()[2]["role"].text() == "build");
    CHECK(d["files"].items()[4]["flagged"].asBool());
    CHECK(!d["files"].items()[4]["warning"].text().empty());
    CHECK(d["files"].items()[1]["archive"].text() == "zip");
    CHECK(d["files"].items()[3]["archive"].text().empty());
    CHECK(d["text"].text() == "A mod for the FNF V-Slice Engine& more.\n- Combos\n- Ratings");
    CHECK(d["license"].text() == "CC BY 4.0, MIT");
    CHECK(d["requirements"].items()[0]["name"].text() == "Friday Night Funkin 0.8.0+");
    CHECK(d["credits"].items()[0]["authors"].items()[0]["name"].text() == "JugieNoob");
    CHECK(d["alternateSources"].items()[0]["url"].text() == "https://github.com/x/releases");
    CHECK(d["archivedFiles"].asNumber() == 1);
    CHECK(d["updates"].asNumber() == 20);

    vbanana::Json other = take(vbanana_parse("mod", R"J({"_idRow":1,"_sModelName":"Mod","_aGame":{"_idRow":6137}})J", "{}"));
    CHECK(!other["ok"].asBool(true));
    vbanana::Json error = take(vbanana_parse("mod", R"J({"_sErrorCode":"404","_sErrorMessage":"Not found"})J", "{}"));
    CHECK(!error["ok"].asBool(true));
    CHECK(error["error"].text().find("Not found") != std::string::npos);
    CHECK(!take(vbanana_parse("mod", "<html>", "{}"))["ok"].asBool(true));

    const char* psych = R"J({"_idRow":2,"_sModelName":"Mod","_sName":"VS Someone","_aGame":{"_idRow":8694},"_aSuperCategory":{"_sName":"Executables"},
        "_aRequirements":[["Psych Engine 0.7.3","https://github.com/ShadowMario/FNF-PsychEngine"]],"_sText":"Made in psych engine",
        "_aFiles":[{"_idRow":9,"_sFile":"vs_someone_mod_folder.zip","_nFilesize":100,"_tsDateAdded":1,"_sAnalysisResult":"ok","_sAvResult":"clean"},
                   {"_idRow":10,"_sFile":"vs_someone_windows.zip","_nFilesize":100,"_tsDateAdded":2,"_sAnalysisResult":"ok","_sAvResult":"clean"}]})J";
    vbanana::Json ps = take(vbanana_parse("mod", psych, "{}"));
    CHECK(ps["engine"]["name"].text() == "Psych Engine");
    CHECK(ps["engine"]["version"].text() == "0.7.3");
    CHECK(ps["best"].asNumber() == 9);
    CHECK(ps["files"].items()[0]["role"].text() == "mod");

    const char* updates = R"J({"_aMetadata":{"_nRecordCount":20,"_bIsComplete":false},"_aRecords":[{"_idRow":478186,"_sName":"Quick Fixes","_sVersion":"1.4.4",
        "_tsDateAdded":1795212642,"_sText":"<b>Fixed</b> stuff","_aChangeLog":[{"text":"Quick fixes","cat":"Bugfix"},{"text":"Removed separators","cat":"Removal"}],
        "_aFileRowIds":[1897608]}]})J";
    vbanana::Json u = take(vbanana_parse("updates", updates, "{}"));
    CHECK(u["total"].asNumber() == 20);
    CHECK(u["updates"].items()[0]["version"].text() == "1.4.4");
    CHECK(u["updates"].items()[0]["changes"].items()[1]["category"].text() == "Removal");
    CHECK(u["updates"].items()[0]["text"].text() == "Fixed stuff");
    CHECK(u["updates"].items()[0]["files"].items()[0].asNumber() == 1897608);
}

static void testInspect() {
    vbanana::Json v = take(vbanana_inspect(R"J(["MyMod/_polymod_meta.json","MyMod/data/songs/bopeebo/bopeebo-chart.json","MyMod/data/songs/bopeebo/bopeebo-metadata.json","MyMod/scripts/stages/X.hxc","__MACOSX/MyMod/._x"])J"));
    CHECK(v["engine"].text() == "V-Slice");
    CHECK(v["root"].text() == "MyMod/");
    CHECK(v["songs"].items().size() == 1);
    CHECK(v["hxc"].asNumber() == 1);
    CHECK(v["entries"].asNumber() == 4);
    vbanana::Json p = take(vbanana_inspect(R"J(["Build/PsychEngine.exe","Build/lime.ndll","Build/mods/vs-x/pack.json","Build/mods/vs-x/data/tutorial/tutorial-hard.json","Build/mods/vs-x/data/tutorial/script.lua","Build/mods/vs-x/custom_events/Flash.lua"])J"));
    CHECK(p["engine"].text() == "Psych Engine");
    CHECK(p["root"].text() == "Build/mods/vs-x/");
    CHECK(p["build"].asBool());
    CHECK(p["modFolders"].items().size() == 1);
    CHECK(p["lua"].asNumber() == 2);
    vbanana::Json c = take(vbanana_inspect(R"J(["cne/data/stages/mall.xml","cne/data/characters/dad.xml","cne/songs/test/charts/hard.json","cne/data/config/modpack.ini"])J"));
    CHECK(c["engine"].text() == "Codename Engine");
    CHECK(c["root"].text() == "cne/");
    vbanana::Json e = take(vbanana_inspect("nope"));
    CHECK(e["engine"].text().empty());
}

static void testMd5() {
    char* empty = vbanana_md5(nullptr, 0);
    CHECK(std::string(empty) == "d41d8cd98f00b204e9800998ecf8427e");
    vbanana_free(empty);
    const char* text = "The quick brown fox jumps over the lazy dog";
    char* fox = vbanana_md5(reinterpret_cast<const unsigned char*>(text), 43);
    CHECK(std::string(fox) == "9e107d9d372bb6826bd81d3542a419d6");
    vbanana_free(fox);
    std::string big(1000, 'a');
    char* a = vbanana_md5(reinterpret_cast<const unsigned char*>(big.data()), big.size());
    CHECK(std::string(a) == "cabe45dcc9ae5b66ba86600cca6b8ba8");
    vbanana_free(a);
    CHECK(vbanana_md5_file("/definitely/missing/file") == nullptr);
}

static void testFuzz() {
    const char* pieces[] = {"{", "}", "[", "]", ",", ":", "\"_aRecords\"", "\"_aFiles\"", "\"_idRow\"", "1", "\"x\"", "null", "true", "\"_sText\"", "\"<b>&#9999999;\"",
        "\"_aGame\"", "\"_aRequirements\"", "\"&amp\""};
    unsigned seed = 3;
    for (int round = 0; round < 4000; round++) {
        std::string text;
        int n = 1 + static_cast<int>(seed % 50);
        for (int i = 0; i < n; i++) {
            seed = seed * 1103515245u + 12345u;
            text += pieces[(seed >> 8) % (sizeof(pieces) / sizeof(pieces[0]))];
        }
        vbanana_free(vbanana_parse("mod", text.c_str(), "{}"));
        vbanana_free(vbanana_parse("page", text.c_str(), "{}"));
        vbanana_free(vbanana_parse("updates", text.c_str(), "{}"));
        vbanana_free(vbanana_inspect(text.c_str()));
        vbanana_free(vbanana_link(text.c_str()));
    }
}

int main() {
    testRequests();
    testLinks();
    testParse();
    testInspect();
    testMd5();
    testFuzz();
    if (failures) {
        std::fprintf(stderr, "v-banana: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("v-banana: all tests passed\n");
    return 0;
}
