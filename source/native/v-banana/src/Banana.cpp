#include "vbanana/VBanana.h"

#include "Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace vbanana {

namespace {

const int kGameId = 8694;
const char* kSite = "https://gamebanana.com";
const char* kApi = "https://gamebanana.com/apiv11";

char* copy(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

double num(const Json& value, double fallback = 0) {
    if (value.kind() == Json::Kind::Number) return value.asNumber();
    if (value.kind() == Json::Kind::String && !value.asString().empty()) {
        char* end = nullptr;
        double d = std::strtod(value.asString().c_str(), &end);
        if (end && *end == '\0') return d;
    }
    if (value.kind() == Json::Kind::Bool) return value.asBool() ? 1 : 0;
    return fallback;
}

bool flag(const Json& value, bool fallback) {
    if (value.kind() == Json::Kind::Bool) return value.asBool();
    if (value.kind() == Json::Kind::Number) return value.asNumber() != 0;
    return fallback;
}

std::string encode(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buffer[4];
            std::snprintf(buffer, sizeof buffer, "%%%02X", c);
            out += buffer;
        }
    }
    return out;
}

void appendUtf8(std::string& out, unsigned long code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x110000) {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

std::string plainText(const std::string& html, size_t limit = 6000) {
    std::string out;
    size_t i = 0;
    while (i < html.size() && out.size() < limit) {
        char c = html[i];
        if (c == '<') {
            size_t end = html.find('>', i);
            if (end == std::string::npos) break;
            std::string tag = lower(html.substr(i + 1, end - i - 1));
            std::string name;
            for (char t : tag) {
                if (std::isalnum(static_cast<unsigned char>(t)) || t == '/') name += t;
                else break;
            }
            if (name == "br" || name == "br/" || name == "/p" || name == "/div" || name == "/h1" || name == "/h2" || name == "/h3" ||
                name == "/ul" || name == "/ol" || name == "/blockquote") {
                out += '\n';
            } else if (name == "li") {
                out += out.empty() || out.back() == '\n' ? "- " : "\n- ";
            }
            i = end + 1;
            continue;
        }
        if (c == '&') {
            size_t end = html.find(';', i);
            if (end != std::string::npos && end - i <= 10) {
                std::string entity = html.substr(i + 1, end - i - 1);
                static const std::map<std::string, std::string> names = {{"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
                    {"nbsp", " "}, {"hellip", "..."}, {"mdash", "-"}, {"ndash", "-"}, {"rsquo", "'"}, {"lsquo", "'"}, {"rdquo", "\""}, {"ldquo", "\""}};
                auto it = names.find(entity);
                if (it != names.end()) {
                    out += it->second;
                    i = end + 1;
                    continue;
                }
                if (!entity.empty() && entity[0] == '#') {
                    unsigned long code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X') ? std::strtoul(entity.c_str() + 2, nullptr, 16)
                                                                                                    : std::strtoul(entity.c_str() + 1, nullptr, 10);
                    appendUtf8(out, code);
                    i = end + 1;
                    continue;
                }
            }
        }
        out += c == '\r' || c == '\t' ? ' ' : c;
        i++;
    }
    std::string clean;
    int newlines = 0;
    bool space = false;
    for (char c : out) {
        if (c == '\n') {
            while (!clean.empty() && clean.back() == ' ') clean.pop_back();
            if (++newlines <= 2 && !clean.empty()) clean += '\n';
            space = false;
            continue;
        }
        if (c == ' ') {
            if (!space && !clean.empty() && clean.back() != '\n') clean += ' ';
            space = true;
            continue;
        }
        newlines = 0;
        space = false;
        clean += c;
    }
    while (!clean.empty() && (clean.back() == '\n' || clean.back() == ' ')) clean.pop_back();
    return clean;
}

struct EngineInfo {
    const char* name;
    std::vector<const char*> words;
};

const std::vector<EngineInfo>& engines() {
    static const std::vector<EngineInfo> list = {
        {"V-Slice", {"v-slice", "vslice", "v slice", "base game", "polymod", ".hxc", "funkin 0.", "friday night funkin 0.", "weekend 1"}},
        {"Psych Engine", {"psych engine", "psych-engine", "psychengine", "psych 0.", "psych 1.", "psych"}},
        {"Codename Engine", {"codename engine", "codename-engine", "codename", "cne"}},
        {"NightmareVision", {"nightmarevision", "nightmare vision"}},
        {"FPS Plus", {"fps plus", "fps+", "fpsplus"}},
        {"Kade Engine", {"kade engine", "kade"}},
        {"Leather Engine", {"leather engine", "leather"}},
        {"JS Engine", {"js engine", "jsengine"}},
        {"P-Slice", {"p-slice", "pslice"}},
        {"Forever Engine", {"forever engine"}}};
    return list;
}

bool wordAt(const std::string& text, size_t at, size_t length) {
    bool left = at == 0 || !std::isalnum(static_cast<unsigned char>(text[at - 1]));
    size_t end = at + length;
    bool right = end >= text.size() || !std::isalpha(static_cast<unsigned char>(text[end])) || text[end - 1] == '.';
    return left && right;
}

int countWord(const std::string& text, const std::string& word) {
    int count = 0;
    size_t at = 0;
    while ((at = text.find(word, at)) != std::string::npos) {
        if (wordAt(text, at, word.size())) count++;
        at += word.size();
    }
    return count;
}

std::string versionAfter(const std::string& text, const std::string& word) {
    size_t at = 0;
    while ((at = text.find(word, at)) != std::string::npos) {
        size_t i = at + word.size();
        size_t limit = std::min(text.size(), i + 12);
        while (i < limit && !std::isdigit(static_cast<unsigned char>(text[i]))) {
            if (std::isalpha(static_cast<unsigned char>(text[i])) && text[i] != 'v') break;
            i++;
        }
        if (i < limit && std::isdigit(static_cast<unsigned char>(text[i]))) {
            std::string v;
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) v += text[i++];
            while (!v.empty() && v.back() == '.') v.pop_back();
            if (v.find('.') != std::string::npos) return v;
        }
        at += word.size();
    }
    return "";
}

struct Detection {
    std::string engine;
    std::string version;
    int confidence = 0;
    std::vector<std::string> reasons;
};

struct Signal {
    std::string text;
    int weight;
    std::string label;
};

Detection detect(const std::vector<Signal>& signals) {
    std::map<std::string, int> score;
    std::map<std::string, std::vector<std::string>> reasons;
    std::map<std::string, std::string> versions;
    for (const Signal& s : signals) {
        std::string text = lower(s.text);
        if (text.empty()) continue;
        for (const EngineInfo& e : engines()) {
            int hits = 0;
            std::string found;
            for (const char* w : e.words) {
                int n = countWord(text, w);
                if (n > 0 && found.empty()) found = w;
                hits += n;
                if (n > 0) {
                    std::string v = versionAfter(text, w);
                    if (!v.empty() && versions[e.name].empty()) versions[e.name] = v;
                }
            }
            if (hits == 0) continue;
            int add = s.weight >= 30 ? s.weight : s.weight * std::min(hits, 3);
            score[e.name] += add;
            reasons[e.name].push_back(s.label + " mentions \"" + found + "\"");
        }
    }
    Detection d;
    int best = 0;
    int second = 0;
    for (const auto& entry : score) {
        if (entry.second > best) {
            second = best;
            best = entry.second;
            d.engine = entry.first;
        } else if (entry.second > second) {
            second = entry.second;
        }
    }
    if (d.engine.empty()) return d;
    d.version = versions[d.engine];
    d.confidence = std::max(5, std::min(100, best - second / 2));
    d.reasons = reasons[d.engine];
    if (d.reasons.size() > 6) d.reasons.resize(6);
    return d;
}

std::string extension(const std::string& name) {
    std::string low = lower(name);
    static const char* exts[] = {".tar.gz", ".zip", ".rar", ".7z", ".tar", ".gz", ".exe", ".apk", ".ipa", ".dmg", ".app", ".msi", ".txt", ".md", ".png", ".jpg", nullptr};
    for (const char** e = exts; *e; e++) if (endsWith(low, *e)) return *e;
    size_t dot = low.rfind('.');
    return dot == std::string::npos ? "" : low.substr(dot);
}

bool archiveExt(const std::string& ext) { return ext == ".zip" || ext == ".rar" || ext == ".7z" || ext == ".tar" || ext == ".tar.gz" || ext == ".gz"; }

bool hasWord(const std::string& text, std::initializer_list<const char*> words) {
    for (const char* w : words) if (countWord(text, w) > 0) return true;
    return false;
}

Json fileJson(const Json& f, const Detection& modEngine) {
    Json out = Json::object();
    long long id = static_cast<long long>(num(f["_idRow"]));
    std::string name = f["_sFile"].text();
    if (name.empty()) name = "file-" + std::to_string(id);
    std::string description = f["_sDescription"].text();
    std::string analysis = lower(f["_sAnalysisResult"].text());
    std::string av = lower(f["_sAvResult"].text());
    bool clean = (analysis.empty() || analysis == "ok") && (av.empty() || av == "clean");
    bool flagged = (!analysis.empty() && analysis != "ok") || (!av.empty() && av != "clean");
    std::string ext = extension(name);
    std::string text = lower(name + " " + description);
    std::string role = "other";
    if (ext == ".exe" || ext == ".apk" || ext == ".ipa" || ext == ".dmg" || ext == ".app" || ext == ".msi" ||
        hasWord(text, {"windows", "win64", "win32", "android", "linux", "macos", "mac", "build", "executable", "exe", "full game", "standalone", "apk"})) {
        role = "build";
    }
    if (hasWord(text, {"source", "src", "source code"})) role = "source";
    if (hasWord(text, {"mod folder", "mods folder", "modpack", "mod only", "polymod", "folder", "mod files", "assets only"})) role = "mod";
    if (hasWord(text, {"patch", "hotfix", "fix only", "update only"})) role = role == "other" ? "patch" : role;
    std::vector<Signal> signals = {{name, 20, "file name"}, {description, 15, "file description"}};
    Detection d = detect(signals);
    double added = num(f["_tsDateAdded"]);
    double size = num(f["_nFilesize"]);
    int score = 0;
    std::string warning;
    if (flagged) {
        score -= 1000;
        warning = "GameBanana's scan flagged this file (" + (av.empty() || av == "clean" ? analysis : av) + ").";
    } else if (clean) {
        score += 100;
    }
    if (archiveExt(ext)) score += 60;
    else score -= 200;
    if (role == "mod") score += 40;
    else if (role == "other") score += 20;
    else if (role == "patch") score += 5;
    else if (role == "source") score -= 30;
    else if (role == "build") score -= 10;
    if (size <= 0) score -= 50;
    if (!d.engine.empty() && d.engine == modEngine.engine) score += 10;
    if (flag(f["_bIsArchived"], false)) score -= 40;
    out.set("id", Json::number(static_cast<double>(id)));
    out.set("name", Json::string(name));
    out.set("size", Json::number(size));
    out.set("description", Json::string(description));
    out.set("downloads", Json::number(num(f["_nDownloadCount"])));
    std::string url = f["_sDownloadUrl"].text();
    out.set("url", Json::string(url.empty() ? std::string(kSite) + "/dl/" + std::to_string(id) : url));
    out.set("md5", Json::string(f["_sMd5Checksum"].text()));
    out.set("added", Json::number(added));
    out.set("clean", Json::boolean(clean));
    out.set("flagged", Json::boolean(flagged));
    out.set("archive", Json::string(archiveExt(ext) ? ext.substr(1) : ""));
    out.set("version", Json::string(f["_sVersion"].text()));
    out.set("role", Json::string(role));
    out.set("engine", Json::string(d.engine));
    out.set("score", Json::number(score));
    out.set("warning", Json::string(warning));
    return out;
}

std::string thumbnail(const Json& record) {
    const Json& images = record["_aPreviewMedia"]["_aImages"];
    if (images.items().empty()) return "";
    const Json& first = images.items()[0];
    std::string base = first["_sBaseUrl"].text();
    std::string file = first["_sFile220"].text();
    if (file.empty()) file = first["_sFile530"].text();
    if (file.empty()) file = first["_sFile"].text();
    if (base.empty() || file.empty()) return "";
    return base + "/" + file;
}

std::vector<std::string> screenshots(const Json& record) {
    std::vector<std::string> out;
    for (const Json& image : record["_aPreviewMedia"]["_aImages"].items()) {
        std::string base = image["_sBaseUrl"].text();
        std::string file = image["_sFile530"].text();
        if (file.empty()) file = image["_sFile"].text();
        if (!base.empty() && !file.empty()) out.push_back(base + "/" + file);
    }
    return out;
}

bool modSummary(const Json& record, Json& out) {
    double id = num(record["_idRow"]);
    if (id <= 0) return false;
    std::string model = record["_sModelName"].text("Mod");
    if (model != "Mod") return false;
    if (!record["_aGame"].isNull() && num(record["_aGame"]["_idRow"], kGameId) != kGameId) return false;
    out = Json::object();
    out.set("id", Json::number(id));
    std::string name = record["_sName"].text();
    out.set("name", Json::string(name.empty() ? "Mod " + std::to_string(static_cast<long long>(id)) : name));
    out.set("submitter", Json::string(record["_aSubmitter"]["_sName"].text()));
    out.set("submitterId", Json::number(num(record["_aSubmitter"]["_idRow"])));
    std::string category = record["_aRootCategory"]["_sName"].text();
    if (category.empty()) category = record["_aCategory"]["_sName"].text();
    out.set("category", Json::string(category));
    out.set("subcategory", Json::string(record["_aSubCategory"]["_sName"].text()));
    out.set("likes", Json::number(num(record["_nLikeCount"])));
    out.set("views", Json::number(num(record["_nViewCount"])));
    out.set("posts", Json::number(num(record["_nPostCount"])));
    out.set("thumbnail", Json::string(thumbnail(record)));
    std::string url = record["_sProfileUrl"].text();
    out.set("profileUrl", Json::string(url.empty() ? std::string(kSite) + "/mods/" + std::to_string(static_cast<long long>(id)) : url));
    out.set("hasFiles", Json::boolean(flag(record["_bHasFiles"], true)));
    out.set("version", Json::string(record["_sVersion"].text()));
    out.set("added", Json::number(num(record["_tsDateAdded"])));
    double updated = num(record["_tsDateUpdated"]);
    if (updated <= 0) updated = num(record["_tsDateModified"]);
    out.set("updated", Json::number(updated));
    out.set("obsolete", Json::boolean(flag(record["_bIsObsolete"], false)));
    std::vector<Signal> signals = {{name, 40, "title"}, {category, 30, "category"}, {record["_aSubCategory"]["_sName"].text(), 30, "subcategory"}};
    std::string tags;
    for (const Json& t : record["_aTags"].items()) tags += (t.kind() == Json::Kind::String ? t.asString() : t["_sValue"].text()) + " ";
    signals.push_back({tags, 25, "tags"});
    Detection d = detect(signals);
    out.set("engine", Json::string(d.engine));
    return true;
}

Json parsePage(const Json& root) {
    Json out = Json::object();
    Json mods = Json::array();
    int count = 0;
    for (const Json& record : root["_aRecords"].items()) {
        Json mod;
        if (modSummary(record, mod)) {
            mods.push(mod);
            count++;
        }
    }
    const Json& meta = root["_aMetadata"];
    out.set("mods", mods);
    out.set("total", Json::number(meta.isNull() ? count : num(meta["_nRecordCount"], count)));
    out.set("complete", Json::boolean(meta.isNull() ? true : flag(meta["_bIsComplete"], true)));
    out.set("perPage", Json::number(num(meta["_nPerpage"], count)));
    return out;
}

Json parseMod(const Json& root) {
    Json out = Json::object();
    double game = root["_aGame"].isNull() ? kGameId : num(root["_aGame"]["_idRow"], kGameId);
    if (game != kGameId) {
        out.set("ok", Json::boolean(false));
        out.set("error", Json::string("That GameBanana page isn't a Friday Night Funkin' mod."));
        return out;
    }
    if (flag(root["_bIsTrashed"], false) || flag(root["_bIsWithheld"], false)) {
        out.set("ok", Json::boolean(false));
        out.set("error", Json::string("This mod was removed or withheld on GameBanana."));
        return out;
    }
    Json mod;
    if (!modSummary(root, mod)) {
        out.set("ok", Json::boolean(false));
        out.set("error", Json::string("GameBanana didn't return a mod."));
        return out;
    }
    std::string text = plainText(root["_sText"].text());
    std::string description = root["_sDescription"].text();
    std::string category = root["_aCategory"]["_sName"].text();
    std::string super = root["_aSuperCategory"]["_sName"].text();
    std::vector<Signal> signals = {{root["_sName"].text(), 40, "title"}, {category, 30, "category"}, {super, 35, "section"},
        {description, 15, "summary"}, {text, 6, "description"}};
    Json requirements = Json::array();
    for (const Json& r : root["_aRequirements"].items()) {
        Json item = Json::object();
        std::string name = r.kind() == Json::Kind::Array && !r.items().empty() ? r.items()[0].text() : r["_sName"].text();
        std::string url = r.kind() == Json::Kind::Array && r.items().size() > 1 ? r.items()[1].text() : r["_sUrl"].text();
        if (name.empty()) continue;
        item.set("name", Json::string(name));
        item.set("url", Json::string(url));
        requirements.push(item);
        std::string low = lower(name + " " + url);
        if ((contains(low, "friday night funkin") || contains(low, "funkin 0.") || contains(low, "ninja-muffin24.itch.io")) && !contains(low, "psych") &&
            !contains(low, "codename") && !contains(low, "kade"))
            signals.push_back({"v-slice " + versionAfter(low, "funkin"), 60, "requirement \"" + name + "\""});
        else signals.push_back({name + " " + url, 60, "requirement \"" + name + "\""});
    }
    std::string tags;
    for (const Json& t : root["_aTags"].items()) tags += (t.kind() == Json::Kind::String ? t.asString() : t["_sValue"].text()) + " ";
    signals.push_back({tags, 25, "tags"});
    Json files = Json::array();
    std::vector<Json> parsedFiles;
    Detection pre = detect(signals);
    for (const Json& f : root["_aFiles"].items()) {
        if (num(f["_idRow"]) <= 0) continue;
        Json file = fileJson(f, pre);
        signals.push_back({f["_sFile"].text() + " " + f["_sDescription"].text(), 10, "file \"" + f["_sFile"].text() + "\""});
        parsedFiles.push_back(file);
    }
    Detection d = detect(signals);
    double bestId = 0;
    double bestScore = -500;
    double bestAdded = -1;
    for (Json& file : parsedFiles) {
        double score = file["score"].asNumber();
        double added = file["added"].asNumber();
        if (score > bestScore || (score == bestScore && added > bestAdded)) {
            bestScore = score;
            bestAdded = added;
            bestId = file["id"].asNumber();
        }
        files.push(file);
    }
    Json credits = Json::array();
    for (const Json& group : root["_aCredits"].items()) {
        Json g = Json::object();
        g.set("group", Json::string(group["_sGroupName"].text()));
        Json authors = Json::array();
        for (const Json& a : group["_aAuthors"].items()) {
            Json author = Json::object();
            author.set("name", Json::string(a["_sName"].text()));
            author.set("role", Json::string(a["_sRole"].text()));
            authors.push(author);
        }
        g.set("authors", authors);
        credits.push(g);
    }
    Json alternate = Json::array();
    for (const Json& a : root["_aAlternateFileSources"].items()) {
        Json item = Json::object();
        item.set("url", Json::string(a["url"].text(a["_sUrl"].text())));
        item.set("description", Json::string(a["description"].text(a["_sDescription"].text())));
        alternate.push(item);
    }
    Json shots = Json::array();
    for (const std::string& s : screenshots(root)) shots.push(Json::string(s));
    Json engine = Json::object();
    engine.set("name", Json::string(d.engine));
    engine.set("version", Json::string(d.version));
    engine.set("confidence", Json::number(d.confidence));
    Json reasons = Json::array();
    for (const std::string& r : d.reasons) reasons.push(Json::string(r));
    engine.set("reasons", reasons);
    mod.set("engine", Json::string(d.engine));
    out.set("ok", Json::boolean(true));
    out.set("mod", mod);
    out.set("downloads", Json::number(num(root["_nDownloadCount"])));
    out.set("description", Json::string(description));
    out.set("text", Json::string(text));
    out.set("license", Json::string(plainText(root["_sLicense"].text(), 300)));
    out.set("section", Json::string(super));
    out.set("requirements", requirements);
    out.set("credits", credits);
    out.set("alternateSources", alternate);
    out.set("screenshots", shots);
    out.set("files", files);
    out.set("archivedFiles", Json::number(static_cast<double>(root["_aArchivedFiles"].items().size())));
    out.set("updates", Json::number(num(root["_nUpdatesCount"])));
    out.set("engine", engine);
    out.set("best", Json::number(bestId));
    return out;
}

Json parseUpdates(const Json& root) {
    Json out = Json::object();
    Json list = Json::array();
    for (const Json& r : root["_aRecords"].items()) {
        Json u = Json::object();
        u.set("id", Json::number(num(r["_idRow"])));
        u.set("name", Json::string(r["_sName"].text()));
        u.set("version", Json::string(r["_sVersion"].text()));
        u.set("added", Json::number(num(r["_tsDateAdded"])));
        u.set("text", Json::string(plainText(r["_sText"].text(), 2000)));
        Json changes = Json::array();
        for (const Json& c : r["_aChangeLog"].items()) {
            Json change = Json::object();
            change.set("text", Json::string(c["text"].text()));
            change.set("category", Json::string(c["cat"].text()));
            changes.push(change);
        }
        u.set("changes", changes);
        Json fileIds = Json::array();
        for (const Json& f : r["_aFileRowIds"].items()) fileIds.push(Json::number(num(f)));
        u.set("files", fileIds);
        list.push(u);
    }
    const Json& meta = root["_aMetadata"];
    out.set("updates", list);
    out.set("total", Json::number(num(meta["_nRecordCount"], static_cast<double>(list.items().size()))));
    out.set("complete", Json::boolean(flag(meta["_bIsComplete"], true)));
    return out;
}

Json request(const Json& r) {
    std::string kind = r["kind"].text();
    int page = std::max(1, static_cast<int>(num(r["page"], 1)));
    int perPage = std::min(50, std::max(1, static_cast<int>(num(r["perPage"], 15))));
    long long id = static_cast<long long>(num(r["id"]));
    std::string url;
    int cache = 0;
    if (kind == "feed") {
        std::string sort = r["sort"].text("default");
        if (sort == "popular") sort = "default";
        if (sort != "default" && sort != "new" && sort != "updated") sort = "default";
        url = std::string(kApi) + "/Game/" + std::to_string(kGameId) + "/Subfeed?_nPage=" + std::to_string(page) + "&_sSort=" + sort + "&_csvModelInclusions=Mod";
        cache = 300;
    } else if (kind == "search") {
        std::string query = r["query"].text();
        size_t a = query.find_first_not_of(" \t\r\n");
        size_t b = query.find_last_not_of(" \t\r\n");
        query = a == std::string::npos ? "" : query.substr(a, b - a + 1);
        if (query.size() < 2) {
            Json out = Json::object();
            out.set("error", Json::string("Type at least two letters to search GameBanana."));
            return out;
        }
        std::string order = r["order"].text("best_match");
        if (order != "best_match" && order != "popularity" && order != "date" && order != "udate") order = "best_match";
        url = std::string(kApi) + "/Util/Search/Results?_sModelName=Mod&_sOrder=" + order + "&_idGameRow=" + std::to_string(kGameId) +
              "&_sSearchString=" + encode(query) + "&_nPage=" + std::to_string(page);
        cache = 120;
    } else if (kind == "mod") {
        if (id <= 0) {
            Json out = Json::object();
            out.set("error", Json::string("A mod id is needed."));
            return out;
        }
        url = std::string(kApi) + "/Mod/" + std::to_string(id) + "/ProfilePage";
        cache = 600;
    } else if (kind == "updates") {
        if (id <= 0) {
            Json out = Json::object();
            out.set("error", Json::string("A mod id is needed."));
            return out;
        }
        url = std::string(kApi) + "/Mod/" + std::to_string(id) + "/Updates?_nPage=" + std::to_string(page) + "&_nPerpage=" + std::to_string(perPage);
        cache = 600;
    } else if (kind == "download") {
        url = std::string(kSite) + "/dl/" + std::to_string(id);
    } else if (kind == "page") {
        url = std::string(kSite) + "/mods/" + std::to_string(id);
    } else {
        Json out = Json::object();
        out.set("error", Json::string("Unknown GameBanana request \"" + kind + "\"."));
        return out;
    }
    Json out = Json::object();
    out.set("url", Json::string(url));
    out.set("method", Json::string("GET"));
    Json headers = Json::object();
    if (kind != "download" && kind != "page") headers.set("Accept", Json::string("application/json"));
    headers.set("User-Agent", Json::string("V-Converter (V-Banana)"));
    out.set("headers", headers);
    out.set("cacheSeconds", Json::number(cache));
    return out;
}

bool digitsAt(const std::string& text, size_t at, long long& value, size_t& end) {
    size_t i = at;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) i++;
    if (i == at || i - at > 12) return false;
    value = std::atoll(text.substr(at, i - at).c_str());
    end = i;
    return value > 0;
}

Json link(const std::string& raw) {
    Json out = Json::object();
    std::string text = raw;
    size_t a = text.find_first_not_of(" \t\r\n<");
    size_t b = text.find_last_not_of(" \t\r\n>");
    text = a == std::string::npos ? "" : text.substr(a, b - a + 1);
    long long value = 0;
    size_t end = 0;
    if (!text.empty() && digitsAt(text, 0, value, end) && end == text.size()) {
        out.set("kind", Json::string("mod"));
        out.set("id", Json::number(static_cast<double>(value)));
        return out;
    }
    std::string low = lower(text);
    size_t host = low.find("gamebanana.com/");
    if (host == std::string::npos) {
        out.set("kind", Json::string("none"));
        return out;
    }
    std::string path = low.substr(host + 15);
    struct Route {
        const char* prefix;
        const char* kind;
    };
    static const Route routes[] = {{"mods/download/", "mod"}, {"mods/", "mod"}, {"wips/", "wip"}, {"tools/download/", "tool"}, {"tools/", "tool"},
        {"sounds/", "sound"}, {"dl/", "file"}, {"mmdl/", "file"}, {"members/", "member"}, {"apiv11/mod/", "mod"}};
    for (const Route& r : routes) {
        size_t n = std::strlen(r.prefix);
        if (path.compare(0, n, r.prefix) == 0 && digitsAt(path, n, value, end)) {
            out.set("kind", Json::string(r.kind));
            out.set("id", Json::number(static_cast<double>(value)));
            return out;
        }
    }
    static const char* params[] = {"_idrow=", "idrow=", "_id=", "id="};
    for (const char* p : params) {
        size_t at = path.find(p);
        if (at != std::string::npos && (at == 0 || path[at - 1] == '?' || path[at - 1] == '&') && digitsAt(path, at + std::strlen(p), value, end)) {
            out.set("kind", Json::string("mod"));
            out.set("id", Json::number(static_cast<double>(value)));
            return out;
        }
    }
    out.set("kind", Json::string("none"));
    return out;
}

std::string dirOf(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

Json inspect(const Json& entries) {
    std::vector<std::string> paths;
    for (const Json& e : entries.items()) {
        std::string p = e.kind() == Json::Kind::String ? e.asString() : e["name"].text();
        std::replace(p.begin(), p.end(), '\\', '/');
        while (!p.empty() && p[0] == '/') p.erase(0, 1);
        if (p.empty() || p.compare(0, 9, "__MACOSX/") == 0 || contains(p, "/__MACOSX/") || endsWith(lower(p), ".ds_store")) continue;
        paths.push_back(p);
    }
    std::map<std::string, int> score;
    std::map<std::string, std::map<std::string, int>> roots;
    std::vector<std::string> executables;
    int lua = 0;
    int hxc = 0;
    int hx = 0;
    int images = 0;
    int audio = 0;
    int videos = 0;
    std::set<std::string> songs;
    std::set<std::string> modFolders;
    auto mark = [&](const std::string& engine, const std::string& root, int weight) {
        score[engine] += weight;
        roots[engine][root] += weight;
    };
    for (const std::string& p : paths) {
        std::string low = lower(p);
        std::string name = low.substr(low.rfind('/') == std::string::npos ? 0 : low.rfind('/') + 1);
        std::string dir = dirOf(p);
        std::string ldir = lower(dir);
        if (endsWith(low, ".lua")) lua++;
        if (endsWith(low, ".hxc")) hxc++;
        if (endsWith(low, ".hx") || endsWith(low, ".hscript")) hx++;
        if (endsWith(low, ".png") || endsWith(low, ".jpg")) images++;
        if (endsWith(low, ".ogg") || endsWith(low, ".mp3") || endsWith(low, ".wav")) audio++;
        if (endsWith(low, ".mp4") || endsWith(low, ".webm")) videos++;
        if (endsWith(low, ".exe") || endsWith(low, ".apk") || endsWith(low, ".app/") || name == "lime.ndll") executables.push_back(p);
        size_t mods = ldir.find("mods/");
        if (mods != std::string::npos && (mods == 0 || ldir[mods - 1] == '/')) {
            size_t start = mods + 5;
            size_t slash = ldir.find('/', start);
            if (slash != std::string::npos && slash > start) modFolders.insert(dir.substr(0, slash + 1));
        }
        if (name == "_polymod_meta.json") mark("V-Slice", dir, 60);
        if (endsWith(low, ".hxc")) {
            size_t at = ldir.find("scripts/");
            mark("V-Slice", at == std::string::npos ? dir : dir.substr(0, at), 4);
        }
        size_t songsAt = ldir.find("data/songs/");
        if (songsAt != std::string::npos && (endsWith(low, "-chart.json") || endsWith(low, "-metadata.json"))) {
            mark("V-Slice", dir.substr(0, songsAt), 10);
            size_t start = songsAt + 11;
            size_t slash = ldir.find('/', start);
            if (slash != std::string::npos) songs.insert(ldir.substr(start, slash - start));
        }
        if (name == "pack.json") mark("Psych Engine", dir, 40);
        if (endsWith(low, ".lua")) {
            static const char* folders[] = {"scripts/", "data/", "stages/", "custom_events/", "custom_notetypes/", "characters/"};
            for (const char* f : folders) {
                size_t at = ldir.find(f);
                if (at != std::string::npos && (at == 0 || ldir[at - 1] == '/')) {
                    mark("Psych Engine", dir.substr(0, at), 5);
                    break;
                }
            }
        }
        if (contains(ldir, "custom_events/") || contains(ldir, "custom_notetypes/")) {
            size_t at = ldir.find("custom_");
            mark("Psych Engine", dir.substr(0, at), 6);
        }
        if (name == "psychengine.exe" || name == "psych engine.exe") mark("Psych Engine", dir, 30);
        size_t cneData = ldir.find("data/stages/");
        if (cneData != std::string::npos && endsWith(low, ".xml")) mark("Codename Engine", dir.substr(0, cneData), 12);
        size_t cneChars = ldir.find("data/characters/");
        if (cneChars != std::string::npos && endsWith(low, ".xml")) mark("Codename Engine", dir.substr(0, cneChars), 12);
        if (name == "modpack.ini" || contains(ldir, "data/config/")) mark("Codename Engine", dir.substr(0, ldir.find("data/") == std::string::npos ? 0 : ldir.find("data/")), 20);
        size_t chartsAt = ldir.find("songs/");
        if (chartsAt != std::string::npos && contains(ldir, "/charts/") && endsWith(low, ".json") && ldir.find("data/songs/") == std::string::npos) {
            mark("Codename Engine", dir.substr(0, chartsAt), 8);
            size_t start = chartsAt + 6;
            size_t slash = ldir.find('/', start);
            if (slash != std::string::npos) songs.insert(ldir.substr(start, slash - start));
        }
        size_t dataAt = ldir.find("data/");
        if (dataAt != std::string::npos && endsWith(low, ".json") && songsAt == std::string::npos && cneData == std::string::npos &&
            cneChars == std::string::npos && !contains(ldir, "data/config/")) {
            std::string rest = ldir.substr(dataAt + 5);
            size_t slash = rest.find('/');
            if (slash != std::string::npos && slash + 1 == rest.size()) {
                std::string song = rest.substr(0, slash);
                std::string stem = name.substr(0, name.size() - 5);
                if (stem == song || stem.compare(0, song.size() + 1, song + "-") == 0) {
                    mark("Psych Engine", dir.substr(0, dataAt), 6);
                    songs.insert(song);
                }
            }
        }
    }
    std::string engine;
    int best = 0;
    int total = 0;
    for (const auto& e : score) {
        total += e.second;
        if (e.second > best) {
            best = e.second;
            engine = e.first;
        }
    }
    std::string root;
    if (!engine.empty()) {
        int bestRoot = -1;
        for (const auto& r : roots[engine]) {
            if (r.second > bestRoot || (r.second == bestRoot && r.first.size() < root.size())) {
                bestRoot = r.second;
                root = r.first;
            }
        }
    }
    Json out = Json::object();
    out.set("engine", Json::string(engine));
    out.set("confidence", Json::number(total > 0 ? std::min(100, best * 100 / total) : 0));
    out.set("root", Json::string(root));
    out.set("build", Json::boolean(!executables.empty()));
    Json exe = Json::array();
    for (size_t i = 0; i < executables.size() && i < 10; i++) exe.push(Json::string(executables[i]));
    out.set("executables", exe);
    Json folders = Json::array();
    for (const std::string& f : modFolders) folders.push(Json::string(f));
    out.set("modFolders", folders);
    Json songList = Json::array();
    for (const std::string& s : songs) songList.push(Json::string(s));
    out.set("songs", songList);
    out.set("entries", Json::number(static_cast<double>(paths.size())));
    out.set("lua", Json::number(lua));
    out.set("hxc", Json::number(hxc));
    out.set("hx", Json::number(hx));
    out.set("images", Json::number(images));
    out.set("audio", Json::number(audio));
    out.set("videos", Json::number(videos));
    return out;
}

class Md5 {
public:
    Md5() { reset(); }

    void update(const unsigned char* data, size_t length) {
        for (size_t i = 0; i < length; i++) {
            buffer_[used_++] = data[i];
            if (used_ == 64) {
                block(buffer_);
                used_ = 0;
            }
        }
        bits_ += static_cast<uint64_t>(length) * 8;
    }

    std::string hex() {
        uint64_t bits = bits_;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (used_ != 56) update(&zero, 1);
        unsigned char size[8];
        for (int i = 0; i < 8; i++) size[i] = static_cast<unsigned char>(bits >> (8 * i));
        update(size, 8);
        char out[33];
        uint32_t words[4] = {a_, b_, c_, d_};
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) std::snprintf(out + i * 8 + j * 2, 3, "%02x", (words[i] >> (8 * j)) & 0xFF);
        }
        out[32] = 0;
        return out;
    }

private:
    uint32_t a_, b_, c_, d_;
    unsigned char buffer_[64];
    size_t used_;
    uint64_t bits_;

    void reset() {
        a_ = 0x67452301;
        b_ = 0xefcdab89;
        c_ = 0x98badcfe;
        d_ = 0x10325476;
        used_ = 0;
        bits_ = 0;
    }

    static uint32_t rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

    void block(const unsigned char* p) {
        static const uint32_t k[64] = {0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8,
            0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
            0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9,
            0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
            0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3,
            0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        static const int r[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
        uint32_t m[16];
        for (int i = 0; i < 16; i++)
            m[i] = static_cast<uint32_t>(p[i * 4]) | (static_cast<uint32_t>(p[i * 4 + 1]) << 8) | (static_cast<uint32_t>(p[i * 4 + 2]) << 16) |
                   (static_cast<uint32_t>(p[i * 4 + 3]) << 24);
        uint32_t a = a_, b = b_, c = c_, d = d_;
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            uint32_t t = d;
            d = c;
            c = b;
            b = b + rotl(a + f + k[i] + m[g], r[i]);
            a = t;
        }
        a_ += a;
        b_ += b;
        c_ += c;
        d_ += d;
    }
};

FILE* openFile(const char* path) {
#ifdef _WIN32
    int size = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (size <= 0) return nullptr;
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, &wide[0], size);
    return _wfopen(wide.c_str(), L"rb");
#else
    return std::fopen(path, "rb");
#endif
}

}

}

extern "C" {

const char* vbanana_version(void) { return "1.0.0"; }

char* vbanana_request(const char* request_json) {
    using namespace vbanana;
    bool ok = false;
    Json r = Json::parse(request_json ? request_json : "{}", ok);
    if (!ok) {
        Json out = Json::object();
        out.set("error", Json::string("The request isn't valid JSON."));
        return copy(out.dump());
    }
    return copy(request(r).dump());
}

char* vbanana_parse(const char* kind, const char* body, const char* options_json) {
    using namespace vbanana;
    (void)options_json;
    std::string k = kind ? kind : "";
    bool ok = false;
    Json root = Json::parse(body ? body : "", ok);
    if (!ok || root.kind() != Json::Kind::Object) {
        Json out = Json::object();
        out.set("ok", Json::boolean(false));
        out.set("error", Json::string("GameBanana sent something that isn't JSON."));
        return copy(out.dump());
    }
    if (!root["_sErrorCode"].isNull() || !root["_sErrorMessage"].isNull()) {
        Json out = Json::object();
        out.set("ok", Json::boolean(false));
        std::string message = root["_sErrorMessage"].text(root["_sErrorCode"].text());
        out.set("error", Json::string("GameBanana answered: " + (message.empty() ? std::string("error") : message)));
        return copy(out.dump());
    }
    Json out;
    if (k == "page") out = parsePage(root);
    else if (k == "mod") out = parseMod(root);
    else if (k == "updates") out = parseUpdates(root);
    else {
        out = Json::object();
        out.set("ok", Json::boolean(false));
        out.set("error", Json::string("Unknown kind \"" + k + "\"."));
        return copy(out.dump());
    }
    if (out["ok"].isNull()) out.set("ok", Json::boolean(true));
    return copy(out.dump());
}

char* vbanana_link(const char* text) { return vbanana::copy(vbanana::link(text ? text : "").dump()); }

char* vbanana_inspect(const char* entries_json) {
    using namespace vbanana;
    bool ok = false;
    Json entries = Json::parse(entries_json ? entries_json : "[]", ok);
    if (!ok) entries = Json::array();
    return copy(inspect(entries).dump());
}

char* vbanana_md5(const unsigned char* data, unsigned long long length) {
    vbanana::Md5 md5;
    if (data && length) md5.update(data, static_cast<size_t>(length));
    return vbanana::copy(md5.hex());
}

char* vbanana_md5_file(const char* path) {
    if (!path) return nullptr;
    FILE* file = vbanana::openFile(path);
    if (!file) return nullptr;
    vbanana::Md5 md5;
    std::vector<unsigned char> buffer(1 << 16);
    size_t read;
    while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) md5.update(buffer.data(), read);
    std::fclose(file);
    return vbanana::copy(md5.hex());
}

void vbanana_free(char* text) { std::free(text); }

}
