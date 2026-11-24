#include "Reverse.hpp"

#include "Funkin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace vscript {

namespace {

const Json kNull;

const Json& field(const Json& value, const std::string& key) {
    return value.kind() == Json::Kind::Object ? value[key] : kNull;
}

std::string asText(const Json& value) {
    switch (value.kind()) {
        case Json::Kind::Null: return "";
        case Json::Kind::Bool: return value.asBool() ? "true" : "false";
        case Json::Kind::Number: return numberToText(value.asNumber());
        case Json::Kind::String: return value.asString();
        default: return kotlinJson(value, 0);
    }
}

double asDouble(const Json& value, double fallback) {
    if (value.kind() == Json::Kind::Number) return std::isfinite(value.asNumber()) ? value.asNumber() : fallback;
    if (value.kind() == Json::Kind::String) {
        std::string t = trimText(value.asString());
        if (t.empty()) return fallback;
        char* end = nullptr;
        double v = std::strtod(t.c_str(), &end);
        if (end == t.c_str() || *end != 0 || !std::isfinite(v)) return fallback;
        return v;
    }
    return fallback;
}

std::string jsonNumberText(double value) {
    double v = jsonNumber(value);
    if (std::fmod(v, 1.0) == 0.0 && std::fabs(v) < 1e15) return std::to_string(static_cast<long long>(v));
    return javaDoubleText(v);
}

Json num(double value) { return Json::number(jsonNumber(value)); }

bool isSpaceChar(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0B; }

std::string formatToSongPath(const std::string& name) {
    std::string out;
    for (char ch : name) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (isSpaceChar(c) || ch == '~' || ch == '&' || ch == ';' || ch == ':' || ch == '<' || ch == '>' || ch == '#') out += '-';
        else if (ch == '.' || ch == ',' || ch == '\'' || ch == '"' || ch == '%' || ch == '?' || ch == '!') continue;
        else out += ch;
    }
    return lowerText(trimText(out));
}

struct TimeChange {
    double time;
    double bpm;
    int numerator;
};

struct Meta {
    std::string songName;
    std::vector<std::string> difficulties;
    std::string player, opponent, girlfriend, stage, noteStyle;
    std::vector<TimeChange> timeChanges;
    double offsetInstrumental = 0;
};

struct Note {
    double time;
    int data;
    double length;
    std::string kind;
};

struct Event {
    double time;
    std::string kind;
    Json value;
};

struct Chart {
    std::vector<std::pair<std::string, double>> scrollSpeed;
    std::vector<Event> events;
    std::vector<std::pair<std::string, std::vector<Note>>> notes;
};

struct Source {
    std::string variation;
    Meta meta;
    Chart chart;
};

std::vector<std::string> strings(const Json& array) {
    std::vector<std::string> out;
    if (array.kind() != Json::Kind::Array) return out;
    for (const Json& item : array.items()) {
        std::string t = asText(item);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

std::string cleanJson(const std::string& raw) {
    std::string t = trimText(raw);
    if (t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF && static_cast<unsigned char>(t[1]) == 0xBB && static_cast<unsigned char>(t[2]) == 0xBF) t.erase(0, 3);
    return t;
}

bool parseMeta(const std::string& raw, Meta& meta, std::string& error) {
    bool ok = false;
    Json root = Json::parse(cleanJson(raw), ok);
    if (!ok || root.kind() != Json::Kind::Object) {
        error = "The metadata is not valid JSON.";
        return false;
    }
    const Json& play = root["playData"];
    if (play.kind() != Json::Kind::Object) {
        error = "This is not a V-Slice song metadata file (no \"playData\").";
        return false;
    }
    const Json& chars = play["characters"];
    meta.songName = asText(root["songName"]);
    meta.difficulties = strings(play["difficulties"]);
    meta.player = asText(field(chars, "player"));
    if (meta.player.empty()) meta.player = "bf";
    meta.opponent = asText(field(chars, "opponent"));
    if (meta.opponent.empty()) meta.opponent = "dad";
    meta.girlfriend = asText(field(chars, "girlfriend"));
    meta.stage = asText(play["stage"]);
    if (meta.stage.empty()) meta.stage = "mainStage";
    meta.noteStyle = asText(play["noteStyle"]);
    if (meta.noteStyle.empty()) meta.noteStyle = "funkin";
    const Json& changes = root["timeChanges"];
    if (changes.kind() == Json::Kind::Array) {
        for (const Json& c : changes.items()) {
            if (c.kind() != Json::Kind::Object) continue;
            int n = static_cast<int>(asDouble(c["n"], 4.0));
            meta.timeChanges.push_back({asDouble(c["t"], 0.0), asDouble(c["bpm"], 100.0), std::max(1, n)});
        }
    }
    std::stable_sort(meta.timeChanges.begin(), meta.timeChanges.end(), [](const TimeChange& a, const TimeChange& b) { return a.time < b.time; });
    if (meta.timeChanges.empty()) meta.timeChanges.push_back({0.0, 100.0, 4});
    meta.offsetInstrumental = asDouble(field(root["offsets"], "instrumental"), 0.0);
    return true;
}

bool parseChart(const std::string& raw, Chart& chart, std::string& error) {
    bool ok = false;
    Json root = Json::parse(cleanJson(raw), ok);
    if (!ok || root.kind() != Json::Kind::Object) {
        error = "The chart is not valid JSON.";
        return false;
    }
    const Json& speeds = root["scrollSpeed"];
    if (speeds.kind() != Json::Kind::Object) {
        error = "This is not a V-Slice chart file (no \"scrollSpeed\").";
        return false;
    }
    for (const auto& f : speeds.fields()) chart.scrollSpeed.emplace_back(f.first, asDouble(f.second, 1.0));
    const Json& notes = root["notes"];
    if (notes.kind() == Json::Kind::Object) {
        for (const auto& f : notes.fields()) {
            std::vector<Note> list;
            if (f.second.kind() == Json::Kind::Array) {
                for (const Json& n : f.second.items()) {
                    if (n.kind() != Json::Kind::Object) continue;
                    list.push_back({asDouble(n["t"], 0.0), static_cast<int>(asDouble(n["d"], 0.0)), asDouble(n["l"], 0.0), asText(n["k"])});
                }
            }
            std::stable_sort(list.begin(), list.end(), [](const Note& a, const Note& b) { return a.time < b.time; });
            chart.notes.emplace_back(f.first, std::move(list));
        }
    }
    const Json& events = root["events"];
    if (events.kind() == Json::Kind::Array) {
        for (const Json& e : events.items()) {
            if (e.kind() != Json::Kind::Object) continue;
            chart.events.push_back({asDouble(e["t"], 0.0), asText(e["e"]), e["v"]});
        }
    }
    std::stable_sort(chart.events.begin(), chart.events.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
    return true;
}

struct Section {
    double start;
    double beats;
    double bpm;
    bool changeBpm;
    bool mustHit = false;
    bool gf = false;
    std::vector<Json> notes;
};

std::vector<Section> buildSections(const Meta& meta, const Chart& chart) {
    double lastNote = 0.0;
    bool anyNote = false;
    for (const auto& d : chart.notes) for (const Note& n : d.second) {
        double end = n.time + n.length;
        if (!anyNote || end > lastNote) lastNote = end;
        anyNote = true;
    }
    double lastEvent = 0.0;
    bool anyEvent = false;
    for (const Event& e : chart.events) {
        if (!anyEvent || e.time > lastEvent) lastEvent = e.time;
        anyEvent = true;
    }
    double end = std::max(lastNote, lastEvent) + 1.0;
    const std::vector<TimeChange>& sorted = meta.timeChanges;
    std::vector<Section> result;
    double time = 0.0;
    size_t changeIndex = 0;
    TimeChange current = sorted.front();
    double previousBpm = current.bpm;
    while (changeIndex + 1 < sorted.size() && sorted[changeIndex + 1].time <= 0.0) current = sorted[++changeIndex];
    int guard = 0;
    while ((time < end || result.empty()) && guard++ < 100000) {
        double beatMs = 60000.0 / current.bpm;
        double full = current.numerator;
        const TimeChange* next = changeIndex + 1 < sorted.size() ? &sorted[changeIndex + 1] : nullptr;
        double length = full * beatMs;
        if (next && next->time < time + length - 0.001) length = next->time - time;
        double scaled = length / beatMs * 1000.0;
        double beats = (std::isnan(scaled) ? 0.0 : std::floor(scaled + 0.5)) / 1000.0;
        result.push_back({time, beats <= 0 ? full : beats, current.bpm, !result.empty() && current.bpm != previousBpm, false, false, {}});
        previousBpm = current.bpm;
        time += length;
        if (next && next->time <= time + 0.001) {
            changeIndex++;
            current = *next;
        }
    }
    if (result.empty()) result.push_back({0.0, 4.0, meta.timeChanges.front().bpm, false, false, false, {}});
    return result;
}

size_t sectionIndex(const std::vector<Section>& sections, double time) {
    size_t lo = 0, hi = sections.size() - 1;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (sections[mid].start <= time + 0.5) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

bool focusChar(const Event& event, int& out) {
    const Json& v = event.value;
    if (v.kind() == Json::Kind::Object) {
        int c = static_cast<int>(asDouble(v["char"], -99.0));
        if (c == -99) return false;
        out = c;
        return true;
    }
    if (v.kind() == Json::Kind::Number) {
        out = static_cast<int>(v.asNumber());
        return true;
    }
    if (v.isNull()) return false;
    std::string t = asText(v);
    if (t.empty()) return false;
    size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
    if (i >= t.size()) return false;
    for (size_t k = i; k < t.size(); k++) if (t[k] < '0' || t[k] > '9') return false;
    long long n = std::atoll(t.c_str());
    if (n < -2147483648LL || n > 2147483647LL) return false;
    out = static_cast<int>(n);
    return true;
}

int applyFocus(std::vector<Section>& sections, const std::vector<const Event*>& focus) {
    int state = 1;
    size_t pointer = 0;
    int lost = 0;
    for (size_t i = 0; i < sections.size(); i++) {
        double sectionEnd = i + 1 < sections.size() ? sections[i + 1].start : 1.7976931348623157e308;
        while (pointer < focus.size() && focus[pointer]->time < sectionEnd - 0.5) {
            int c = 0;
            if (focusChar(*focus[pointer], c) && c >= 0) {
                if (focus[pointer]->time > sections[i].start + 1.0) lost++;
                state = c;
            }
            pointer++;
        }
        sections[i].mustHit = state == 0;
        sections[i].gf = state == 2;
    }
    return lost;
}

std::string target(const std::string& raw) {
    std::string l = lowerText(raw);
    if (l == "bf" || l == "boyfriend" || l == "player" || l == "0") return "BF";
    if (l == "gf" || l == "girlfriend" || l == "2") return "GF";
    return "Dad";
}

bool equalsIgnoreCase(const std::string& a, const std::string& b) { return lowerText(a) == lowerText(b); }

double stepMs(const Meta& meta, double time) {
    const TimeChange* change = nullptr;
    for (const TimeChange& c : meta.timeChanges) if (c.time <= time) change = &c;
    if (!change) change = &meta.timeChanges.front();
    return 15000.0 / change->bpm;
}

std::string stageFor(const std::string& stage, const Json& stages) {
    std::string key = lowerText(stage);
    const Json& mapped = stages[key];
    return mapped.kind() == Json::Kind::String ? mapped.asString() : stage;
}

std::string noteTypeFor(const std::string& kind) {
    if (kind.empty() || kind == "normal") return "";
    std::string l = lowerText(kind);
    if (l == "alt") return "Alt Animation";
    if (l == "hey") return "Hey!";
    if (l == "hurt") return "Hurt Note";
    if (l == "noanim") return "No Animation";
    if (l == "gf-sing") return "GF Sing";
    return kind;
}

using Grouped = std::vector<std::pair<double, std::vector<std::vector<std::string>>>>;

Grouped psychEvents(const std::vector<Event>& events, const Meta& meta, double speed, bool hasZoom, double stageZoom, const Json& names, std::map<std::string, int>& skipped) {
    std::vector<std::pair<double, std::vector<std::vector<std::string>>>> grouped;
    std::map<double, size_t> index;
    auto add = [&](double time, const std::string& name, const std::string& v1, const std::string& v2) {
        auto it = index.find(time);
        if (it == index.end()) {
            index[time] = grouped.size();
            grouped.push_back({time, {}});
            it = index.find(time);
        }
        grouped[it->second].second.push_back({name, v1, v2});
    };
    auto skip = [&](const std::string& kind) { skipped[kind]++; };
    for (const Event& event : events) {
        const Json& v = event.value;
        if (event.kind == "FocusCamera") {
            int c = 0;
            if (focusChar(event, c) && c == -1) add(event.time, "Camera Follow Pos", asText(field(v, "x")), asText(field(v, "y")));
        } else if (event.kind == "PlayAnimation") {
            std::string anim = asText(field(v, "anim"));
            std::string who = target(asText(field(v, "target")));
            if (equalsIgnoreCase(anim, "hey") || equalsIgnoreCase(anim, "cheer")) add(event.time, "Hey!", who == "GF" ? "GF" : "BF", "0.6");
            else if (!anim.empty()) add(event.time, "Play Animation", anim, who);
        } else if (event.kind == "ScrollSpeed") {
            double scroll = asDouble(field(v, "scroll"), 1.0);
            bool absolute = asText(field(v, "absolute")) == "true";
            double multiplier = absolute && speed > 0 ? scroll / speed : scroll;
            bool instant = equalsIgnoreCase(asText(field(v, "ease")), "INSTANT");
            double seconds = instant ? 0.0 : asDouble(field(v, "duration"), 4.0) * stepMs(meta, event.time) / 1000.0;
            add(event.time, "Change Scroll Speed", jsonNumberText(multiplier), jsonNumberText(seconds));
        } else if (event.kind == "ZoomCamera") {
            double zoom = asDouble(field(v, "zoom"), 1.0);
            std::string mode = asText(field(v, "mode"));
            if (mode == "direct") add(event.time, "Set Property", "defaultCamZoom", jsonNumberText(zoom));
            else if (hasZoom) add(event.time, "Set Property", "defaultCamZoom", jsonNumberText(zoom * stageZoom));
            else skip("ZoomCamera (stage mode)");
        } else if (event.kind == "SetCameraBop") {
            add(event.time, "Set Property", "camZoomingMult", jsonNumberText(asDouble(field(v, "intensity"), 1.0)));
        } else if (event.kind == "PsychEvent") {
            add(event.time, asText(field(v, "name")), asText(field(v, "value1")), asText(field(v, "value2")));
        } else {
            bool hasValues = false;
            if (v.kind() == Json::Kind::Object) {
                for (const auto& f : v.fields()) if (f.first == "value1" || f.first == "value2") hasValues = true;
            }
            if (hasValues) {
                const Json& mapped = names[event.kind];
                add(event.time, mapped.kind() == Json::Kind::String ? mapped.asString() : event.kind, asText(v["value1"]), asText(v["value2"]));
            } else {
                add(event.time, event.kind, v.isNull() ? "" : (v.kind() == Json::Kind::Object || v.kind() == Json::Kind::Array) ? kotlinJson(v, 0) : asText(v), "");
                skip(event.kind);
            }
        }
    }
    std::stable_sort(grouped.begin(), grouped.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return grouped;
}

std::string songName(const std::string& id, const Meta& meta) {
    std::string name = trimText(meta.songName);
    return !name.empty() && formatToSongPath(name) == id ? name : id;
}

std::string capitalize(std::string text) {
    if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

Json convertGroup(const std::string& songId, std::vector<Source*>& sources, bool hasZoom, double stageZoom, const Json& options, NamedTexts& files) {
    std::string id = formatToSongPath(songId);
    if (id.empty()) id = "song";
    ReportOut report;
    std::vector<std::string> difficulties;
    std::vector<std::string> paths;
    std::map<std::string, int> skipped;
    const Meta* mainMeta = &sources.front()->meta;
    for (Source* s : sources) if (s->variation.empty()) { mainMeta = &s->meta; break; }
    std::string name = songName(id, *mainMeta);
    int lostFocus = 0;
    std::vector<Source*> ordered = sources;
    std::stable_sort(ordered.begin(), ordered.end(), [](const Source* a, const Source* b) { return a->variation < b->variation; });
    const Json& stages = options["stages"];
    const Json& names = options["eventNames"];
    for (Source* source : ordered) {
        const Meta& meta = source->meta;
        std::vector<Section> sections = buildSections(meta, source->chart);
        std::vector<const Event*> focus;
        for (const Event& e : source->chart.events) if (e.kind == "FocusCamera") focus.push_back(&e);
        std::vector<std::string> diffs = meta.difficulties;
        if (diffs.empty()) for (const auto& d : source->chart.notes) diffs.push_back(d.first);
        for (const std::string& difficulty : diffs) {
            static const std::vector<Note> none;
            const std::vector<Note>* notes = &none;
            for (const auto& d : source->chart.notes) if (d.first == difficulty) { notes = &d.second; break; }
            double speed = 1.0;
            bool found = false;
            for (const auto& s : source->chart.scrollSpeed) if (s.first == difficulty) { speed = s.second; found = true; break; }
            if (!found) for (const auto& s : source->chart.scrollSpeed) if (s.first == "default") { speed = s.second; found = true; break; }
            if (!found && !source->chart.scrollSpeed.empty()) speed = source->chart.scrollSpeed.front().second;
            std::vector<Section> built = sections;
            for (Section& s : built) s.notes.clear();
            for (const Note& note : *notes) {
                size_t index = sectionIndex(built, note.time);
                Json entry = Json::array();
                entry.push(num(note.time));
                entry.push(Json::number(std::clamp(note.data, 0, 7)));
                entry.push(num(note.length));
                std::string type = noteTypeFor(note.kind);
                if (!type.empty()) entry.push(Json::string(type));
                built[index].notes.push_back(std::move(entry));
            }
            lostFocus = applyFocus(built, focus);
            Json events = Json::array();
            for (const auto& group : psychEvents(source->chart.events, meta, speed, hasZoom, stageZoom, names, skipped)) {
                Json list = Json::array();
                for (const auto& e : group.second) {
                    Json triple = Json::array();
                    for (const std::string& part : e) triple.push(Json::string(part));
                    list.push(std::move(triple));
                }
                Json pair = Json::array();
                pair.push(num(group.first));
                pair.push(std::move(list));
                events.push(std::move(pair));
            }
            Json notesJson = Json::array();
            for (Section& s : built) {
                Json obj = Json::object();
                Json sectionNotes = Json::array();
                for (Json& n : s.notes) sectionNotes.push(std::move(n));
                obj.set("sectionNotes", std::move(sectionNotes));
                obj.set("sectionBeats", num(s.beats));
                obj.set("mustHitSection", Json::boolean(s.mustHit));
                obj.set("gfSection", Json::boolean(s.gf));
                obj.set("altAnim", Json::boolean(false));
                obj.set("bpm", num(s.bpm));
                obj.set("changeBPM", Json::boolean(s.changeBpm));
                notesJson.push(std::move(obj));
            }
            Json root = Json::object();
            root.set("song", Json::string(name));
            root.set("notes", std::move(notesJson));
            root.set("events", std::move(events));
            root.set("bpm", num(meta.timeChanges.front().bpm));
            root.set("needsVoices", Json::boolean(true));
            root.set("speed", num(speed));
            root.set("offset", num(meta.offsetInstrumental));
            root.set("player1", Json::string(meta.player));
            root.set("player2", Json::string(meta.opponent));
            root.set("gfVersion", Json::string(meta.girlfriend.empty() ? "gf" : meta.girlfriend));
            root.set("stage", Json::string(stageFor(meta.stage, stages)));
            root.set("format", Json::string("psych_v1"));
            if (equalsIgnoreCase(meta.noteStyle, "pixel")) {
                root.set("arrowSkin", Json::string("pixelUI/NOTE_assets"));
                root.set("splashSkin", Json::string("pixelUI/noteSplashes"));
            }
            std::string lower = lowerText(difficulty);
            std::string suffix = equalsIgnoreCase(difficulty, "normal") ? "" : "-" + lower;
            std::string path = "data/" + id + "/" + id + suffix + ".json";
            files.emplace_back(path, kotlinJson(root, 0));
            paths.push_back(path);
            difficulties.push_back(lower);
            report.stat(capitalize(difficulty), Json::number(double(notes->size())));
        }
    }
    std::vector<std::string> listed;
    for (const std::string& d : difficulties) listed.push_back("data/" + id + "/" + id + (d == "normal" ? "" : "-" + d) + ".json");
    report.section("Difficulties", listed);
    bool extra = false;
    for (const std::string& d : difficulties) if (d != "easy" && d != "normal" && d != "hard") extra = true;
    if (extra) report.warn("Psych only lists Easy/Normal/Hard by default. Add the extra difficulties to the week's \"difficulties\" field (the Weeks converter does this).");
    if (lostFocus > 0) report.warn(std::to_string(lostFocus) + " camera focus changes happen in the middle of a section; Psych moves the camera once per section, so they snap to the section they fall in.");
    if (!skipped.empty()) {
        std::vector<std::string> lines;
        for (const auto& s : skipped) lines.push_back(s.first + " \xC3\x97" + std::to_string(s.second));
        report.section("Events with no Psych equivalent (kept under their V-Slice names for scripts)", lines);
    }
    if (mainMeta->offsetInstrumental != 0.0) report.section("Audio offset", {"The V-Slice instrumental offset (" + jsonNumberText(mainMeta->offsetInstrumental) + " ms) became the Psych chart offset."});
    if (equalsIgnoreCase(mainMeta->noteStyle, "pixel")) report.warn("Pixel note style: the charts use Psych's pixelUI skins; make sure the stage is a pixel stage (isPixelStage).");
    report.section("Audio", {"songs/" + id + "/Inst.ogg", "songs/" + id + "/Voices.ogg or Voices-Player.ogg + Voices-Opponent.ogg"});
    if (name != mainMeta->songName && !trimText(mainMeta->songName).empty()) {
        report.warn("Psych finds songs by name, so \"" + mainMeta->songName + "\" is saved as \"" + name + "\" to match the folder.");
    }
    Json out = Json::object();
    out.set("songId", Json::string(id));
    out.set("songName", Json::string(name));
    Json diffs = Json::array();
    std::vector<std::string> seen;
    for (const std::string& d : difficulties) {
        if (std::find(seen.begin(), seen.end(), d) != seen.end()) continue;
        seen.push_back(d);
        diffs.push(Json::string(d));
    }
    out.set("difficulties", std::move(diffs));
    Json fileList = Json::array();
    for (const std::string& p : paths) fileList.push(Json::string(p));
    out.set("files", std::move(fileList));
    out.set("report", report.toJson());
    return out;
}

}

Json convertVSliceToPsychCharts(const NamedTexts& inputs, const Json& options, NamedTexts& files, std::string& error) {
    std::map<std::string, Meta> metas;
    std::vector<Source> sources;
    for (const auto& input : inputs) {
        size_t colon = input.first.find(':');
        std::string role = input.first.substr(0, colon);
        std::string variation = colon == std::string::npos ? "" : input.first.substr(colon + 1);
        if (role == "metadata") {
            Meta meta;
            if (!parseMeta(input.second, meta, error)) return Json();
            metas[variation] = std::move(meta);
        }
    }
    for (const auto& input : inputs) {
        size_t colon = input.first.find(':');
        std::string role = input.first.substr(0, colon);
        std::string variation = colon == std::string::npos ? "" : input.first.substr(colon + 1);
        if (role != "chart") continue;
        auto meta = metas.find(variation);
        if (meta == metas.end()) continue;
        Source source;
        source.variation = variation;
        source.meta = meta->second;
        if (!parseChart(input.second, source.chart, error)) return Json();
        sources.push_back(std::move(source));
    }
    if (sources.empty()) {
        error = "Add the song's metadata and chart files.";
        return Json();
    }
    std::string songId = options["songId"].text();
    bool hasZoom = options["stageZoom"].kind() == Json::Kind::Number;
    double zoom = options["stageZoom"].asNumber(1.0);
    Json results = Json::array();
    if (options["single"].asBool(false)) {
        std::vector<Source*> group;
        for (Source& s : sources) group.push_back(&s);
        results.push(convertGroup(songId, group, hasZoom, zoom, options, files));
    } else {
        std::map<std::string, std::vector<Source*>> byVariation;
        for (Source& s : sources) byVariation[s.variation].push_back(&s);
        for (auto& entry : byVariation) {
            std::string id = formatToSongPath(entry.first.empty() || entry.first == "default" ? songId : songId + "-" + entry.first);
            if (id.empty()) id = "song";
            results.push(convertGroup(id, entry.second, hasZoom, zoom, options, files));
        }
    }
    Json out = Json::object();
    out.set("results", std::move(results));
    return out;
}

Json convertFiles(const std::string& kind, const NamedTexts& inputs, const Json& options, NamedTexts& files, std::string& error) {
    if (kind == "vslice-psych-charts") return convertVSliceToPsychCharts(inputs, options, files, error);
    if (kind == "json-keys") {
        Json out = Json::array();
        for (const auto& input : inputs) {
            bool ok = false;
            Json root = Json::parse(cleanJson(input.second), ok);
            Json entry = Json::object();
            entry.set("valid", Json::boolean(ok));
            Json keys = Json::array();
            if (ok && root.kind() == Json::Kind::Object) {
                for (const auto& f : root.fields()) {
                    std::string kind = f.second.kind() == Json::Kind::Object ? "object" : f.second.kind() == Json::Kind::Array ? "array" : f.second.kind() == Json::Kind::String ? "string" : f.second.kind() == Json::Kind::Number ? "number" : f.second.kind() == Json::Kind::Bool ? "bool" : "null";
                    Json pair = Json::array();
                    pair.push(Json::string(f.first));
                    pair.push(Json::string(kind));
                    keys.push(std::move(pair));
                }
            }
            entry.set("keys", std::move(keys));
            out.push(std::move(entry));
        }
        Json result = Json::object();
        result.set("entries", std::move(out));
        return result;
    }
    error = "Unknown conversion \"" + kind + "\".";
    return Json();
}

}
