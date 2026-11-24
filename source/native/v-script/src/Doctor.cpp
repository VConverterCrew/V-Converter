#include "Doctor.hpp"

#include "ApiData.hpp"
#include "Haxe.hpp"
#include "Lua.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace vscript {

namespace {

struct Edit {
    size_t start;
    size_t end;
    std::string text;
};

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

int distance(const std::string& a, const std::string& b) {
    size_t n = a.size();
    size_t m = b.size();
    if (n > m + 3 || m > n + 3) return 99;
    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1));
    for (size_t i = 0; i <= n; i++) d[i][0] = static_cast<int>(i);
    for (size_t j = 0; j <= m; j++) d[0][j] = static_cast<int>(j);
    for (size_t i = 1; i <= n; i++) {
        for (size_t j = 1; j <= m; j++) {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    }
    return d[n][m];
}

std::string nearest(const std::string& name, const std::vector<std::string>& candidates) {
    std::string low = lower(name);
    for (const std::string& c : candidates) {
        if (lower(c) == low) return c;
    }
    int limit = name.size() >= 8 ? 2 : name.size() >= 4 ? 1 : 0;
    if (limit == 0) return "";
    int best = limit + 1;
    std::string found;
    bool tie = false;
    for (const std::string& c : candidates) {
        int d = distance(low, lower(c));
        if (d < best) {
            best = d;
            found = c;
            tie = false;
        } else if (d == best) {
            tie = true;
        }
    }
    if (best > limit || tie) return "";
    return found;
}

std::string applyEdits(const std::string& text, std::vector<Edit> edits) {
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.start != b.start ? a.start > b.start : a.end > b.end; });
    std::string out = text;
    size_t limit = std::string::npos;
    for (const Edit& e : edits) {
        if (e.end > limit || e.end > out.size() || e.start > e.end) continue;
        out.replace(e.start, e.end - e.start, e.text);
        limit = e.start;
    }
    return out;
}

int lineAt(const std::string& text, size_t pos) {
    int line = 1;
    for (size_t i = 0; i < pos && i < text.size(); i++) if (text[i] == '\n') line++;
    return line;
}

std::string utf8(unsigned long code) {
    std::string out;
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    return out;
}

enum class TK { Name, Number, String, Op, Comment, Space, Newline, Other };

struct Tok {
    TK k;
    size_t s;
    size_t e;
    int line;
    std::string t;
    char quote = 0;
};

class Report {
public:
    std::vector<Issue> fixes;
    std::vector<Issue> problems;
    std::set<std::string> seen;

    void fix(int line, const std::string& message) {
        if (seen.insert("f" + std::to_string(line) + message).second) fixes.push_back({line, message});
    }
    void problem(int line, const std::string& message) {
        if (seen.insert("p" + std::to_string(line) + message).second) problems.push_back({line, message});
    }
};

std::vector<Tok> luaTokens(const std::string& src) {
    std::vector<Tok> out;
    size_t i = 0;
    int line = 1;
    auto push = [&](TK k, size_t s, size_t e, char q = 0) {
        Tok t{k, s, e, line, src.substr(s, e - s), q};
        out.push_back(t);
    };
    auto longBracket = [&](size_t at) -> int {
        if (at >= src.size() || src[at] != '[') return -1;
        size_t j = at + 1;
        int level = 0;
        while (j < src.size() && src[j] == '=') {
            level++;
            j++;
        }
        return j < src.size() && src[j] == '[' ? level : -1;
    };
    auto closeLong = [&](size_t from, int level) -> size_t {
        std::string close = "]" + std::string(static_cast<size_t>(level), '=') + "]";
        size_t at = src.find(close, from);
        return at == std::string::npos ? src.size() : at + close.size();
    };
    while (i < src.size()) {
        char c = src[i];
        size_t start = i;
        int startLine = line;
        if (c == '\n') {
            push(TK::Newline, i, i + 1);
            line++;
            i++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            while (i < src.size() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r')) i++;
            push(TK::Space, start, i);
            continue;
        }
        if (c == '-' && i + 1 < src.size() && src[i + 1] == '-') {
            int level = longBracket(i + 2);
            if (level >= 0) i = closeLong(i + 4 + static_cast<size_t>(level), level);
            else while (i < src.size() && src[i] != '\n') i++;
            Tok t{TK::Comment, start, i, startLine, src.substr(start, i - start)};
            out.push_back(t);
            for (size_t k = start; k < i; k++) if (src[k] == '\n') line++;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && (src[i + 1] == '/' || src[i + 1] == '*')) {
            if (src[i + 1] == '/') {
                while (i < src.size() && src[i] != '\n') i++;
            } else {
                size_t end = src.find("*/", i + 2);
                i = end == std::string::npos ? src.size() : end + 2;
            }
            Tok t{TK::Comment, start, i, startLine, src.substr(start, i - start)};
            out.push_back(t);
            for (size_t k = start; k < i; k++) if (src[k] == '\n') line++;
            continue;
        }
        if (c == '"' || c == '\'') {
            i++;
            while (i < src.size() && src[i] != c && src[i] != '\n') i += src[i] == '\\' ? 2 : 1;
            i = std::min(i + 1, src.size());
            push(TK::String, start, i, c);
            continue;
        }
        if (c == '[') {
            int level = longBracket(i);
            if (level >= 0) {
                i = closeLong(i + 2 + static_cast<size_t>(level), level);
                Tok t{TK::String, start, i, startLine, src.substr(start, i - start), '['};
                out.push_back(t);
                for (size_t k = start; k < i; k++) if (src[k] == '\n') line++;
                continue;
            }
        }
        if (identStart(c)) {
            while (i < src.size() && identPart(src[i])) i++;
            push(TK::Name, start, i);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
            while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '.' ||
                                      ((src[i] == '+' || src[i] == '-') && (src[i - 1] == 'e' || src[i - 1] == 'E'))))
                i++;
            push(TK::Number, start, i);
            continue;
        }
        static const char* ops[] = {"...", "..=", "..", "==", "~=", "<=", ">=", "::", "!=", "&&", "||", "+=", "-=", "*=", "/=", "++", nullptr};
        bool matched = false;
        for (const char** op = ops; *op; op++) {
            size_t n = std::strlen(*op);
            if (src.compare(i, n, *op) == 0) {
                i += n;
                push(TK::Op, start, i);
                matched = true;
                break;
            }
        }
        if (matched) continue;
        i++;
        push(TK::Op, start, i);
    }
    return out;
}

bool significant(const Tok& t) { return t.k != TK::Space && t.k != TK::Newline && t.k != TK::Comment; }

int prevSig(const std::vector<Tok>& toks, int i) {
    for (int j = i - 1; j >= 0; j--) if (significant(toks[static_cast<size_t>(j)])) return j;
    return -1;
}

int nextSig(const std::vector<Tok>& toks, int i) {
    for (size_t j = static_cast<size_t>(i) + 1; j < toks.size(); j++) if (significant(toks[j])) return static_cast<int>(j);
    return -1;
}

int matchClose(const std::vector<Tok>& toks, int open, const char* o, const char* c) {
    int depth = 0;
    for (size_t j = static_cast<size_t>(open); j < toks.size(); j++) {
        if (toks[j].k != TK::Op) continue;
        if (toks[j].t == o) depth++;
        else if (toks[j].t == c && --depth == 0) return static_cast<int>(j);
    }
    return -1;
}

struct Args {
    std::vector<std::pair<size_t, size_t>> ranges;
    bool open = false;
};

Args callArgs(const std::vector<Tok>& toks, int open, int close, const std::string& src) {
    Args a;
    size_t start = toks[static_cast<size_t>(open)].e;
    int depth = 0;
    bool any = false;
    for (int j = open + 1; j < close; j++) {
        const Tok& t = toks[static_cast<size_t>(j)];
        if (t.k == TK::Op && (t.t == "(" || t.t == "{" || t.t == "[")) depth++;
        if (t.k == TK::Op && (t.t == ")" || t.t == "}" || t.t == "]")) depth--;
        if (significant(t)) any = true;
        if (depth == 0 && t.k == TK::Op && t.t == ",") {
            a.ranges.push_back({start, t.s});
            start = t.e;
        }
        if (t.k == TK::Op && t.t == "...") a.open = true;
    }
    if (any) a.ranges.push_back({start, toks[static_cast<size_t>(close)].s});
    if (!a.ranges.empty()) {
        std::string last = src.substr(a.ranges.back().first, a.ranges.back().second - a.ranges.back().first);
        size_t end = last.find_last_not_of(" \t\r\n");
        if (end != std::string::npos && last[end] == ')') a.open = true;
    }
    return a;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

const std::set<std::string> kLuaKeywords = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local",
    "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};

const std::set<std::string> kLuaStd = {"print", "type", "tostring", "tonumber", "pairs", "ipairs", "next", "select", "unpack", "pcall", "xpcall",
    "error", "assert", "require", "setmetatable", "getmetatable", "rawget", "rawset", "rawequal", "rawlen", "loadstring", "load", "dofile",
    "collectgarbage", "os", "string", "table", "math", "io", "coroutine", "bit", "debug", "jit", "_G", "module", "self", "arg", "newproxy", "gcinfo"};

const std::map<std::string, std::string> kCallbackAliases = {{"create", "onCreate"}, {"postCreate", "onCreatePost"}, {"update", "onUpdate"},
    {"postUpdate", "onUpdatePost"}, {"beatHit", "onBeatHit"}, {"stepHit", "onStepHit"}, {"sectionHit", "onSectionHit"}, {"measureHit", "onSectionHit"},
    {"onSongEnd", "onEndSong"}, {"onCountdownStep", "onCountdownTick"}, {"onCountdownStart", "onStartCountdown"}, {"onDadHit", "opponentNoteHit"},
    {"onPlayerHit", "goodNoteHit"}, {"onPlayerMiss", "noteMiss"}, {"destroy", "onDestroy"}, {"onSongEvent", "onEvent"}};

std::string quoted(const std::string& text) {
    if (text.size() >= 2 && (text[0] == '\'' || text[0] == '"') && text.back() == text[0] && text.find('\\') == std::string::npos) return text.substr(1, text.size() - 2);
    return "";
}

class LuaDoctor {
public:
    LuaDoctor(const std::string& source, const DoctorOptions& options) : src_(source), opt_(options) {
        for (size_t i = 0; i < kPsychFunctionCount; i++) {
            api_[kPsychFunctions[i].name] = &kPsychFunctions[i];
            apiNames_.push_back(kPsychFunctions[i].name);
        }
        for (size_t i = 0; i < kPsychDeprecatedCount; i++) deprecated_[kPsychDeprecated[i].from] = kPsychDeprecated[i].to;
        deprecated_["musicFadeIn"] = "soundFadeIn";
        deprecated_["musicFadeOut"] = "soundFadeOut";
        for (size_t i = 0; i < kPsychCallbackCount; i++) callbacks_.push_back(kPsychCallbacks[i]);
        for (size_t i = 0; i < kPsychGlobalCount; i++) globals_.insert(kPsychGlobals[i]);
        for (size_t i = 0; i < kPsychClassCount; i++) classes_[kPsychClasses[i].from] = kPsychClasses[i].to;
    }

    DoctorResult run() {
        syntaxIsms();
        repair();
        for (int pass = 0; pass < 4; pass++) {
            if (!apiPass()) break;
        }
        callbackNames();
        checkSyntax();
        DoctorResult r;
        r.source = src_;
        r.fixes = report_.fixes;
        r.problems = report_.problems;
        return r;
    }

private:
    std::string src_;
    DoctorOptions opt_;
    Report report_;
    std::map<std::string, const LuaApiFunction*> api_;
    std::vector<std::string> apiNames_;
    std::map<std::string, std::string> deprecated_;
    std::vector<std::string> callbacks_;
    std::set<std::string> globals_;
    std::map<std::string, std::string> classes_;

    void syntaxIsms() {
        std::vector<Tok> toks = luaTokens(src_);
        std::vector<Edit> edits;
        std::set<std::string> declared;
        for (size_t i = 0; i < toks.size(); i++) {
            if (toks[i].k != TK::Name) continue;
            int n = nextSig(toks, static_cast<int>(i));
            int p = prevSig(toks, static_cast<int>(i));
            if (n >= 0 && toks[static_cast<size_t>(n)].t == "=" && (p < 0 || toks[static_cast<size_t>(p)].t != ".")) declared.insert(toks[i].t);
            if (p >= 0 && toks[static_cast<size_t>(p)].t == "local") declared.insert(toks[i].t);
        }
        int inParams = 0;
        for (size_t i = 0; i < toks.size(); i++) {
            const Tok& t = toks[i];
            int ii = static_cast<int>(i);
            if (t.k == TK::Comment && t.t.compare(0, 2, "//") == 0) {
                edits.push_back({t.s, t.s + 2, "--"});
                report_.fix(t.line, "\"//\" comments are not Lua; changed to \"--\".");
                continue;
            }
            if (t.k == TK::Comment && t.t.compare(0, 2, "/*") == 0) {
                std::string inner = t.t.substr(2, t.t.size() >= 4 ? t.t.size() - 4 : 0);
                std::string eq;
                while (inner.find("]" + eq + "]") != std::string::npos) eq += "=";
                edits.push_back({t.s, t.e, "--[" + eq + "[" + inner + "]" + eq + "]"});
                report_.fix(t.line, "\"/* */\" comments are not Lua; changed to \"--[[ ]]\".");
                continue;
            }
            if (t.k == TK::Name && t.t == "function") {
                int n = nextSig(toks, ii);
                while (n >= 0 && (toks[static_cast<size_t>(n)].k == TK::Name || toks[static_cast<size_t>(n)].t == "." || toks[static_cast<size_t>(n)].t == ":")) n = nextSig(toks, n);
                if (n >= 0 && toks[static_cast<size_t>(n)].t == "(") inParams = 1;
                continue;
            }
            if (inParams > 0 && t.k == TK::Op) {
                if (t.t == "(") {
                    inParams++;
                    if (inParams == 2) continue;
                }
                if (t.t == ")") {
                    inParams--;
                    if (inParams <= 1) inParams = 0;
                    continue;
                }
                if (t.t == ":" && inParams == 2) {
                    int n = nextSig(toks, ii);
                    if (n >= 0 && toks[static_cast<size_t>(n)].k == TK::Name) {
                        size_t end = toks[static_cast<size_t>(n)].e;
                        int after = nextSig(toks, n);
                        if (after >= 0 && toks[static_cast<size_t>(after)].t == "<") {
                            int close = matchClose(toks, after, "<", ">");
                            if (close > 0) end = toks[static_cast<size_t>(close)].e;
                        }
                        size_t start = t.s;
                        if (i > 0 && toks[i - 1].k == TK::Space) start = toks[i - 1].s;
                        edits.push_back({start, end, ""});
                        report_.fix(t.line, "removed a Haxe type annotation from the function parameters.");
                    }
                    continue;
                }
            }
            if (t.k != TK::Op && t.k != TK::Name) continue;
            if (t.k == TK::Op && t.t == "!=") {
                edits.push_back({t.s, t.e, "~="});
                report_.fix(t.line, "\"!=\" is \"~=\" in Lua.");
            } else if (t.k == TK::Op && (t.t == "&&" || t.t == "||")) {
                std::string word = t.t == "&&" ? "and" : "or";
                bool left = t.s > 0 && !std::isspace(static_cast<unsigned char>(src_[t.s - 1]));
                bool right = t.e < src_.size() && !std::isspace(static_cast<unsigned char>(src_[t.e]));
                edits.push_back({t.s, t.e, (left ? " " : "") + word + (right ? " " : "")});
                report_.fix(t.line, "\"" + t.t + "\" is \"" + word + "\" in Lua.");
            } else if (t.k == TK::Op && t.t == "!") {
                edits.push_back({t.s, t.e, "not "});
                report_.fix(t.line, "\"!\" is \"not\" in Lua.");
            } else if (t.k == TK::Op && (t.t == "+=" || t.t == "-=" || t.t == "*=" || t.t == "/=" || t.t == "..=")) {
                std::string lv = statementTarget(toks, ii);
                if (!lv.empty()) {
                    std::string op = t.t.substr(0, t.t.size() - 1);
                    edits.push_back({t.s, t.e, "= " + lv + " " + op});
                    report_.fix(t.line, "Lua has no \"" + t.t + "\"; wrote \"" + lv + " = " + lv + " " + op + " ...\".");
                }
            } else if (t.k == TK::Op && t.t == "++") {
                std::string lv = statementTarget(toks, ii);
                int n = nextSig(toks, ii);
                bool endOfStatement = n < 0 || toks[static_cast<size_t>(n)].line != t.line || toks[static_cast<size_t>(n)].t == "end" || toks[static_cast<size_t>(n)].t == ";";
                if (!lv.empty() && endOfStatement) {
                    edits.push_back({t.s, t.e, " = " + lv + " + 1"});
                    report_.fix(t.line, "Lua has no \"++\"; wrote \"" + lv + " = " + lv + " + 1\".");
                }
            } else if (t.k == TK::Name && t.t == "var") {
                int n = nextSig(toks, ii);
                int p = prevSig(toks, ii);
                if (n >= 0 && toks[static_cast<size_t>(n)].k == TK::Name && !kLuaKeywords.count(toks[static_cast<size_t>(n)].t) &&
                    (p < 0 || toks[static_cast<size_t>(p)].t != ".")) {
                    edits.push_back({t.s, t.e, "local"});
                    report_.fix(t.line, "\"var\" is \"local\" in Lua.");
                }
            } else if (t.k == TK::Name && t.t == "null" && !declared.count("null")) {
                int p = prevSig(toks, ii);
                if (p < 0 || toks[static_cast<size_t>(p)].t != ".") {
                    edits.push_back({t.s, t.e, "nil"});
                    report_.fix(t.line, "\"null\" is \"nil\" in Lua.");
                }
            }
        }
        src_ = applyEdits(src_, edits);
    }

    std::string statementTarget(const std::vector<Tok>& toks, int op) {
        static const std::set<std::string> stops = {"then", "do", "else", "end", ";", "repeat", "return"};
        int j = prevSig(toks, op);
        int first = -1;
        while (j >= 0) {
            const Tok& t = toks[static_cast<size_t>(j)];
            if (t.line != toks[static_cast<size_t>(op)].line) break;
            if ((t.k == TK::Name && stops.count(t.t)) || (t.k == TK::Op && t.t == ";")) break;
            if (!(t.k == TK::Name || t.k == TK::Number || t.k == TK::String || t.t == "." || t.t == "[" || t.t == "]")) return "";
            first = j;
            j = prevSig(toks, j);
        }
        if (first < 0) return "";
        std::string text = trim(src_.substr(toks[static_cast<size_t>(first)].s, toks[static_cast<size_t>(prevSig(toks, op))].e - toks[static_cast<size_t>(first)].s));
        if (text.empty() || !identStart(text[0])) return "";
        if (kLuaKeywords.count(text)) return "";
        return text;
    }

    bool parses(std::string& message, int& line) {
        try {
            parseLua(src_);
            return true;
        } catch (const LuaError& e) {
            message = e.what();
            line = e.line;
            return false;
        }
    }

    size_t lineEnd(int line, bool beforeComment) {
        std::vector<Tok> toks = luaTokens(src_);
        size_t end = std::string::npos;
        for (const Tok& t : toks) {
            if (t.line != line) continue;
            if (t.k == TK::Newline) break;
            if (beforeComment && t.k == TK::Comment) break;
            if (t.k != TK::Space) end = t.e;
        }
        return end;
    }

    bool lineHas(int line, const std::set<std::string>& words) {
        for (const Tok& t : luaTokens(src_)) if (t.line == line && t.k == TK::Name && words.count(t.t)) return true;
        return false;
    }

    void repair() {
        std::string message;
        int line = 0;
        for (int round = 0; round < 12; round++) {
            if (parses(message, line)) return;
            std::string before = src_;
            if (message.find("expected 'then'") != std::string::npos || message.find("expected 'do'") != std::string::npos) {
                bool then = message.find("expected 'then'") != std::string::npos;
                std::set<std::string> heads = then ? std::set<std::string>{"if", "elseif"} : std::set<std::string>{"for", "while"};
                for (int at = line; at >= 1 && at >= line - 6; at--) {
                    if (!lineHas(at, heads)) continue;
                    size_t end = lineEnd(at, true);
                    if (end != std::string::npos) {
                        src_.insert(end, then ? " then" : " do");
                        report_.fix(at, then ? "added the missing \"then\"." : "added the missing \"do\".");
                    }
                    break;
                }
            } else if (message.find("expected 'end'") != std::string::npos && (message.find("end of file") != std::string::npos || message.find("<eof>") != std::string::npos || message.find("near ''") != std::string::npos)) {
                std::vector<Tok> toks = luaTokens(src_);
                bool replaced = false;
                for (size_t i = 0; i + 1 < toks.size(); i++) {
                    if (toks[i].k == TK::Name && toks[i].t == "else") {
                        int n = nextSig(toks, static_cast<int>(i));
                        if (n >= 0 && toks[static_cast<size_t>(n)].t == "if" && toks[static_cast<size_t>(n)].line == toks[i].line) {
                            std::string trial = src_;
                            trial.replace(toks[i].s, toks[static_cast<size_t>(n)].e - toks[i].s, "elseif");
                            std::string saved = src_;
                            src_ = trial;
                            std::string m2;
                            int l2 = 0;
                            bool ok = parses(m2, l2);
                            if (ok || l2 > line || m2 != message) {
                                report_.fix(toks[i].line, "\"else if\" is \"elseif\" in Lua.");
                                replaced = true;
                                break;
                            }
                            src_ = saved;
                        }
                    }
                }
                if (!replaced) {
                    if (!src_.empty() && src_.back() != '\n') src_ += "\n";
                    src_ += "end\n";
                    report_.fix(lineAt(src_, src_.size() - 1), "added a missing \"end\" at the end of the script.");
                }
            } else if ((message.find("'end' was not expected") != std::string::npos || message.find("unexpected 'end'") != std::string::npos ||
                        message.find("near 'end'") != std::string::npos)) {
                std::vector<Tok> toks = luaTokens(src_);
                for (size_t i = toks.size(); i-- > 0;) {
                    if (toks[i].line == line && toks[i].k == TK::Name && toks[i].t == "end") {
                        src_.erase(toks[i].s, toks[i].e - toks[i].s);
                        report_.fix(line, "removed an extra \"end\".");
                        break;
                    }
                }
            }
            if (src_ == before) return;
            std::string m2;
            int l2 = 0;
            if (!parses(m2, l2) && l2 < line && m2 == message) {
                src_ = before;
                return;
            }
        }
    }

    void checkSyntax() {
        std::string message;
        int line = 0;
        if (!parses(message, line)) report_.problem(line, "Lua syntax error: " + message + ".");
    }

    struct Defs {
        std::set<std::string> names;
        std::map<std::string, std::vector<int>> globalFunctions;
        std::set<std::string> called;
    };

    Defs definitions(const std::vector<Tok>& toks) {
        Defs d;
        for (size_t i = 0; i < toks.size(); i++) {
            const Tok& t = toks[i];
            if (t.k != TK::Name) continue;
            int ii = static_cast<int>(i);
            int p = prevSig(toks, ii);
            int n = nextSig(toks, ii);
            std::string prev = p >= 0 ? toks[static_cast<size_t>(p)].t : "";
            std::string next = n >= 0 ? toks[static_cast<size_t>(n)].t : "";
            if (prev == "function") {
                if (next != "." && next != ":") {
                    d.names.insert(t.t);
                    int pp = prevSig(toks, p);
                    if (pp < 0 || toks[static_cast<size_t>(pp)].t != "local") d.globalFunctions[t.t].push_back(t.line);
                }
            }
            if (prev == "local" || prev == "for" || (prev == "," && next != "(")) d.names.insert(t.t);
            if (next == "=" && prev != "." && prev != ":") d.names.insert(t.t);
            if (next == "(" && prev != "function" && prev != "." && prev != ":") d.called.insert(t.t);
        }
        for (size_t i = 0; i < toks.size(); i++) {
            if (toks[i].k != TK::Name || toks[i].t != "function") continue;
            int open = nextSig(toks, static_cast<int>(i));
            while (open >= 0 && toks[static_cast<size_t>(open)].t != "(") open = nextSig(toks, open);
            if (open < 0) continue;
            int close = matchClose(toks, open, "(", ")");
            for (int j = open + 1; j < close; j++) if (toks[static_cast<size_t>(j)].k == TK::Name) d.names.insert(toks[static_cast<size_t>(j)].t);
        }
        for (size_t i = 0; i < toks.size(); i++) {
            if (toks[i].k != TK::Name || toks[i].t != "for") continue;
            for (int j = nextSig(toks, static_cast<int>(i)); j >= 0 && toks[static_cast<size_t>(j)].t != "in" && toks[static_cast<size_t>(j)].t != "=" && toks[static_cast<size_t>(j)].t != "do"; j = nextSig(toks, j))
                if (toks[static_cast<size_t>(j)].k == TK::Name) d.names.insert(toks[static_cast<size_t>(j)].t);
        }
        return d;
    }

    std::string argText(const Args& a, size_t i) { return trim(src_.substr(a.ranges[i].first, a.ranges[i].second - a.ranges[i].first)); }

    std::string joinTag(const std::string& tag, const std::string& field) {
        std::string a = quoted(tag);
        std::string b = quoted(field);
        if (!a.empty() && !b.empty()) return "'" + a + "." + b + "'";
        return tag + " .. '.' .. " + field;
    }

    bool deprecatedCall(const std::string& name, const Args& a, std::string& out) {
        std::vector<std::string> args;
        for (size_t i = 0; i < a.ranges.size(); i++) args.push_back(argText(a, i));
        std::string to = deprecated_[name];
        if (name == "objectPlayAnimation" && args.size() >= 4) args.insert(args.begin() + 3, "false");
        if (name == "characterPlayAnim" && !args.empty()) {
            std::string c = lower(quoted(args[0]));
            if (c == "bf" || c == "boyfriend") args[0] = "'boyfriend'";
            if (c == "girlfriend" || c == "gf") args[0] = "'gf'";
        }
        if (name == "addAnimationByIndicesLoop") {
            if (args.size() == 4) args.push_back("24");
            if (args.size() >= 5) args.insert(args.begin() + 5, "true");
        }
        if (name == "getPropertyLuaSprite") {
            if (args.size() < 2) return false;
            args = {joinTag(args[0], args[1])};
        }
        if (name == "setPropertyLuaSprite") {
            if (args.size() < 3) return false;
            args = {joinTag(args[0], args[1]), args[2]};
        }
        if (name == "musicFadeIn" || name == "musicFadeOut") args.insert(args.begin(), "''");
        out = to + "(";
        for (size_t i = 0; i < args.size(); i++) out += (i ? ", " : "") + args[i];
        out += ")";
        return true;
    }

    bool apiPass() {
        std::vector<Tok> toks = luaTokens(src_);
        Defs defs = definitions(toks);
        std::vector<Edit> edits;
        std::vector<std::string> known = apiNames_;
        for (const std::string& n : defs.names) known.push_back(n);
        for (size_t i = 0; i < toks.size(); i++) {
            const Tok& t = toks[i];
            if (t.k != TK::Name || kLuaKeywords.count(t.t)) continue;
            int ii = static_cast<int>(i);
            int p = prevSig(toks, ii);
            int n = nextSig(toks, ii);
            if (n < 0 || toks[static_cast<size_t>(n)].t != "(") continue;
            std::string prev = p >= 0 ? toks[static_cast<size_t>(p)].t : "";
            if (prev == "." || prev == ":" || prev == "function") continue;
            const std::string& name = t.t;
            if (defs.names.count(name) && !deprecated_.count(name)) continue;
            int close = matchClose(toks, n, "(", ")");
            if (close < 0) continue;
            Args args = callArgs(toks, n, close, src_);
            if (deprecated_.count(name)) {
                std::string out;
                if (deprecatedCall(name, args, out)) {
                    edits.push_back({t.s, toks[static_cast<size_t>(close)].e, out});
                    report_.fix(t.line, name + "() is deprecated in Psych; changed to " + deprecated_[name] + "().");
                }
                continue;
            }
            auto api = api_.find(name);
            if (api != api_.end()) {
                checkArgs(*api->second, args, t.line);
                classArgs(name, toks, args, edits, t.line);
                continue;
            }
            if (kLuaStd.count(name) || globals_.count(name)) continue;
            if (name == "trace") {
                edits.push_back({t.s, t.e, "debugPrint"});
                report_.fix(t.line, "trace() doesn't exist in Psych Lua; changed to debugPrint().");
                continue;
            }
            std::string fixName = nearest(name, known);
            if (!fixName.empty() && fixName != name) {
                edits.push_back({t.s, t.e, fixName});
                report_.fix(t.line, name + "() doesn't exist; changed to " + fixName + "().");
                continue;
            }
            report_.problem(t.line, name + "() isn't a Psych Lua function and isn't defined in this script.");
        }
        if (edits.empty()) return false;
        src_ = applyEdits(src_, edits);
        return true;
    }

    void checkArgs(const LuaApiFunction& f, const Args& args, int line) {
        int count = static_cast<int>(args.ranges.size());
        if (count > f.total && !args.open) report_.problem(line, std::string(f.name) + "() takes at most " + std::to_string(f.total) + " argument(s) (" + f.params + ") but got " + std::to_string(count) + ".");
    }

    void classArgs(const std::string& name, const std::vector<Tok>& toks, const Args& args, std::vector<Edit>& edits, int line) {
        static const std::set<std::string> fromClass = {"getPropertyFromClass", "setPropertyFromClass", "callMethodFromClass", "createInstance", "addHaxeLibrary"};
        if (!fromClass.count(name) || args.ranges.empty()) return;
        (void)toks;
        std::string cls = argText(args, 0);
        std::string bare = quoted(cls);
        std::string resolved = bare;
        if (!bare.empty() && bare.find('.') == std::string::npos) {
            auto it = classes_.find(bare);
            if (it != classes_.end()) {
                resolved = it->second;
                size_t start = args.ranges[0].first + (src_.substr(args.ranges[0].first, args.ranges[0].second - args.ranges[0].first).find(cls));
                edits.push_back({start + 1, start + 1 + bare.size(), resolved});
                report_.fix(line, "Psych 0.7+ needs the full class path: '" + bare + "' is '" + resolved + "'.");
            }
        }
        if (resolved == "backend.ClientPrefs" && args.ranges.size() > 1 && (name == "getPropertyFromClass" || name == "setPropertyFromClass")) {
            std::string fieldText = argText(args, 1);
            std::string field = quoted(fieldText);
            static const std::set<std::string> statics = {"data", "defaultData", "keyBinds", "gamepadBinds", "defaultKeys", "defaultButtons"};
            if (!field.empty() && field.compare(0, 5, "data.") != 0 && !statics.count(field.substr(0, field.find('.')))) {
                size_t start = args.ranges[1].first + src_.substr(args.ranges[1].first, args.ranges[1].second - args.ranges[1].first).find(fieldText);
                edits.push_back({start + 1, start + 1, "data."});
                report_.fix(line, "ClientPrefs options live under 'data.' since Psych 0.7: '" + field + "' is 'data." + field + "'.");
            }
        }
    }

    void callbackNames() {
        std::vector<Tok> toks = luaTokens(src_);
        Defs defs = definitions(toks);
        std::vector<Edit> edits;
        std::set<std::string> callbackSet(callbacks_.begin(), callbacks_.end());
        for (const auto& fn : defs.globalFunctions) {
            const std::string& name = fn.first;
            if (fn.second.size() > 1 && callbackSet.count(name)) report_.problem(fn.second[1], name + "() is defined more than once; only the last one runs.");
            if (callbackSet.count(name) || defs.called.count(name)) continue;
            std::string fixed;
            auto alias = kCallbackAliases.find(name);
            if (alias != kCallbackAliases.end()) fixed = alias->second;
            else if (name.size() >= 6) fixed = nearest(name, callbacks_);
            if (fixed.empty() || fixed == name || defs.globalFunctions.count(fixed)) continue;
            for (size_t i = 0; i < toks.size(); i++) {
                if (toks[i].k != TK::Name || toks[i].t != name) continue;
                int p = prevSig(toks, static_cast<int>(i));
                if (p >= 0 && toks[static_cast<size_t>(p)].t == "function") {
                    edits.push_back({toks[i].s, toks[i].e, fixed});
                    report_.fix(toks[i].line, "Psych never calls " + name + "(); renamed it to the callback " + fixed + "().");
                }
            }
        }
        src_ = applyEdits(src_, edits);
    }
};

std::vector<Tok> haxeTokens(const std::string& src) {
    std::vector<Tok> out;
    size_t i = 0;
    int line = 1;
    while (i < src.size()) {
        char c = src[i];
        size_t start = i;
        int startLine = line;
        if (c == '\n') {
            out.push_back({TK::Newline, i, i + 1, line, "\n"});
            line++;
            i++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            while (i < src.size() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r')) i++;
            out.push_back({TK::Space, start, i, line, src.substr(start, i - start)});
            continue;
        }
        if (c == '/' && i + 1 < src.size() && (src[i + 1] == '/' || src[i + 1] == '*')) {
            if (src[i + 1] == '/') {
                while (i < src.size() && src[i] != '\n') i++;
            } else {
                size_t end = src.find("*/", i + 2);
                i = end == std::string::npos ? src.size() : end + 2;
            }
            out.push_back({TK::Comment, start, i, startLine, src.substr(start, i - start)});
            for (size_t k = start; k < i; k++) if (src[k] == '\n') line++;
            continue;
        }
        if (c == '#' && (start == 0 || src[start - 1] == '\n' || std::isspace(static_cast<unsigned char>(src[start - 1])))) {
            while (i < src.size() && src[i] != '\n') i++;
            out.push_back({TK::Comment, start, i, startLine, src.substr(start, i - start)});
            continue;
        }
        if (c == '"' || c == '\'') {
            i++;
            while (i < src.size() && src[i] != c) {
                if (src[i] == '\\') i++;
                else if (src[i] == '\n') line++;
                i++;
            }
            i = std::min(i + 1, src.size());
            Tok t{TK::String, start, i, startLine, src.substr(start, i - start), c};
            out.push_back(t);
            continue;
        }
        if (identStart(c)) {
            while (i < src.size() && identPart(src[i])) i++;
            out.push_back({TK::Name, start, i, line, src.substr(start, i - start)});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || (src[i] == '.' && i + 1 < src.size() && src[i + 1] != '.'))) i++;
            out.push_back({TK::Number, start, i, line, src.substr(start, i - start)});
            continue;
        }
        static const char* ops[] = {"...", "?.", "=>", "->", "==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "??", nullptr};
        bool matched = false;
        for (const char** op = ops; *op; op++) {
            size_t n = std::strlen(*op);
            if (src.compare(i, n, *op) == 0) {
                i += n;
                out.push_back({TK::Op, start, i, line, src.substr(start, i - start)});
                matched = true;
                break;
            }
        }
        if (matched) continue;
        i++;
        out.push_back({TK::Op, start, i, line, src.substr(start, i - start)});
    }
    return out;
}

const std::set<std::string> kHaxeStd = {"Std", "Math", "String", "Int", "Float", "Bool", "Array", "Dynamic", "Void", "Map", "StringTools", "Reflect",
    "Type", "Lambda", "Date", "EReg", "Null", "Class", "Enum", "Any", "Iterator", "Iterable", "UInt", "Single", "Xml", "StringBuf", "List",
    "Assets", "Paths", "Preferences", "Constants", "FlxG", "DateTools", "IntIterator", "ArrayAccess"};

const std::set<std::string> kPolymodAliases = {"lime.utils.Assets", "openfl.utils.Assets", "funkin.modding.base.ScriptedFunkinSprite",
    "funkin.modding.base.ScriptedMusicBeatState", "funkin.modding.base.ScriptedMusicBeatSubState", "funkin.data.dialogue.conversation.ConversationRegistry",
    "funkin.data.dialogue.dialoguebox.DialogueBoxRegistry", "funkin.data.dialogue.speaker.SpeakerRegistry", "funkin.play.character.CharacterDataParser",
    "funkin.play.character.CharacterData.CharacterDataParser", "funkin.graphics.adobeanimate.FlxAtlasSprite", "funkin.modding.base.ScriptedFlxAtlasSprite",
    "funkin.util.FileUtil", "funkin.api.newgrounds.Leaderboards", "funkin.api.newgrounds.Medals", "funkin.api.newgrounds.NewgroundsClient",
    "funkin.api.discord.DiscordClient"};

const std::set<std::string> kBlacklisted = {"Sys", "cpp.Lib", "haxe.Http", "haxe.Unserializer", "lime.utils.AssetLibrary", "lime.system.CFFI",
    "lime.system.JNI", "lime.system.System", "openfl.Lib", "openfl.system.ApplicationDomain", "openfl.net.SharedObject", "openfl.desktop.NativeProcess"};

const std::set<std::string> kPsychPackages = {"states", "backend", "objects", "psychlua", "substates", "options", "cutscenes", "shaders", "debug"};

class HaxeDoctor {
public:
    HaxeDoctor(const std::string& source, const DoctorOptions& options) : src_(source), opt_(options) {
        int line = 1;
        for (char c : src_) {
            origin_.push_back(line);
            if (c == '\n') line++;
        }
        origin_.push_back(line);
        for (size_t i = 0; i < kHaxeTypeCount; i++) types_[kHaxeTypes[i].name] = &kHaxeTypes[i];
        for (size_t i = 0; i < kHaxePathCount; i++) paths_.insert(kHaxePaths[i]);
        for (size_t i = 0; i < kVSliceCallbackCount; i++) events_[kVSliceCallbacks[i].from] = kVSliceCallbacks[i].to;
        for (size_t i = 0; i < kScriptBaseCount; i++) bases_[kScriptBases[i].from] = kScriptBases[i].to;
    }

    DoctorResult run() {
        escapes();
        if (opt_.engine == "vslice") {
            for (int pass = 0; pass < 2; pass++) structure();
            psychIsms();
            imports();
        } else {
            duplicateImports();
        }
        try {
            parseHaxe(src_);
        } catch (const HaxeError& e) {
            report_.problem(e.line, std::string("Haxe syntax error: ") + e.what() + ".");
        }
        DoctorResult r;
        r.source = src_;
        r.fixes = report_.fixes;
        r.problems = report_.problems;
        return r;
    }

private:
    std::string src_;
    DoctorOptions opt_;
    Report report_;
    std::map<std::string, const HaxeType*> types_;
    std::set<std::string> paths_;
    std::map<std::string, std::string> events_;
    std::map<std::string, std::string> bases_;
    std::vector<int> origin_;

    std::vector<Tok> tokens() {
        std::vector<Tok> toks = haxeTokens(src_);
        for (Tok& t : toks) t.line = t.s < origin_.size() ? origin_[t.s] : origin_.back();
        return toks;
    }

    void apply(std::vector<Edit> edits) {
        std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.start != b.start ? a.start > b.start : a.end > b.end; });
        size_t limit = std::string::npos;
        for (const Edit& e : edits) {
            if (e.end > limit || e.end > src_.size() || e.start > e.end) continue;
            int line = e.start < origin_.size() ? origin_[e.start] : origin_.back();
            src_.replace(e.start, e.end - e.start, e.text);
            origin_.erase(origin_.begin() + static_cast<long>(e.start), origin_.begin() + static_cast<long>(e.end));
            origin_.insert(origin_.begin() + static_cast<long>(e.start), e.text.size(), line);
            limit = e.start;
        }
    }

    void escapes() {
        std::vector<Tok> toks = tokens();
        std::vector<Edit> edits;
        for (const Tok& t : toks) {
            if (t.k != TK::String || t.t.size() < 2) continue;
            for (size_t i = t.s + 1; i + 1 < t.e; i++) {
                if (src_[i] != '\\') continue;
                char e = src_[i + 1];
                if (e == 'n' || e == 'r' || e == 't' || e == '\\' || e == '\'' || e == '"') {
                    i++;
                    continue;
                }
                std::string rep;
                size_t end = i + 2;
                int line = origin_[i];
                if (e == 'x') {
                    std::string hex;
                    while (end < t.e - 1 && hex.size() < 2 && std::isxdigit(static_cast<unsigned char>(src_[end]))) hex += src_[end++];
                    unsigned long code = hex.empty() ? 0 : std::strtoul(hex.c_str(), nullptr, 16);
                    rep = escapedChar(code, t.quote);
                } else if (e == 'u') {
                    std::string hex;
                    if (end < t.e - 1 && src_[end] == '{') {
                        end++;
                        while (end < t.e - 1 && src_[end] != '}') hex += src_[end++];
                        end++;
                    } else {
                        while (end < t.e - 1 && hex.size() < 4 && std::isxdigit(static_cast<unsigned char>(src_[end]))) hex += src_[end++];
                    }
                    unsigned long code = hex.empty() ? 0 : std::strtoul(hex.c_str(), nullptr, 16);
                    rep = code < 0x80 ? escapedChar(code, t.quote) : utf8(code);
                } else if (e == '$') {
                    rep = t.quote == '\'' ? "$$" : "$";
                } else if (e == '0') {
                    rep = "";
                } else {
                    static const std::map<char, char> controls = {{'a', '\a'}, {'b', '\b'}, {'f', '\f'}, {'v', '\v'}, {'e', 27}};
                    auto c = controls.find(e);
                    rep = c != controls.end() ? std::string(1, c->second) : std::string(1, e);
                }
                edits.push_back({i, end, rep});
                report_.fix(line, std::string("Polymod only reads \\n, \\r, \\t, \\\\, \\' and \\\" in strings; rewrote \\") + e + ".");
                i = end - 1;
            }
        }
        apply(edits);
    }

    static std::string escapedChar(unsigned long code, char quote) {
        if (code == '\n') return "\\n";
        if (code == '\r') return "\\r";
        if (code == '\t') return "\\t";
        if (code == '\\') return "\\\\";
        if (code == static_cast<unsigned long>(quote)) return quote == '\'' ? "\\'" : "\\\"";
        if (code == 0) return "";
        return std::string(1, static_cast<char>(code));
    }

    struct Import {
        size_t start;
        size_t end;
        std::string path;
        std::string alias;
        int line;
    };

    std::vector<Import> importList(const std::vector<Tok>& toks) {
        std::vector<Import> out;
        for (size_t i = 0; i < toks.size(); i++) {
            if (toks[i].k != TK::Name || (toks[i].t != "import" && toks[i].t != "using")) continue;
            int p = prevSig(toks, static_cast<int>(i));
            if (p >= 0 && toks[static_cast<size_t>(p)].t != ";" && toks[static_cast<size_t>(p)].t != "}" && toks[static_cast<size_t>(p)].k != TK::Comment) continue;
            Import im;
            im.start = toks[i].s;
            im.line = toks[i].line;
            std::string path;
            size_t j = i + 1;
            bool alias = false;
            for (; j < toks.size(); j++) {
                const Tok& t = toks[j];
                if (t.t == ";") break;
                if (!significant(t)) continue;
                if (t.k == TK::Name && (t.t == "as" || t.t == "in")) {
                    alias = true;
                    continue;
                }
                if (alias) im.alias += t.t;
                else path += t.t;
            }
            if (j >= toks.size()) break;
            im.path = path;
            im.end = toks[j].e;
            if (im.end < src_.size() && src_[im.end] == '\n') im.end++;
            else if (im.end + 1 < src_.size() && src_[im.end] == '\r' && src_[im.end + 1] == '\n') im.end += 2;
            out.push_back(im);
            i = j;
        }
        return out;
    }

    static bool psychClass(const std::string& name) {
        for (size_t i = 0; i < kPsychClassCount; i++) if (name == kPsychClasses[i].from) return true;
        return false;
    }

    static std::string simpleName(const std::string& path) {
        size_t dot = path.rfind('.');
        return dot == std::string::npos ? path : path.substr(dot + 1);
    }

    static std::string dropModule(const std::string& path) {
        size_t last = path.rfind('.');
        if (last == std::string::npos) return path;
        size_t prev = path.rfind('.', last - 1);
        if (prev == std::string::npos) return path.substr(last + 1);
        std::string module = path.substr(prev + 1, last - prev - 1);
        if (module.empty() || !std::isupper(static_cast<unsigned char>(module[0]))) return path;
        return path.substr(0, prev) + path.substr(last);
    }

    bool validPath(const std::string& path) {
        return paths_.count(path) || paths_.count(dropModule(path)) || kPolymodAliases.count(path) || path.back() == '*';
    }

    void duplicateImports() {
        std::vector<Tok> toks = tokens();
        std::vector<Import> list = importList(toks);
        std::set<std::string> seen;
        std::vector<Edit> edits;
        for (const Import& im : list) {
            std::string key = im.path + " as " + im.alias;
            if (!seen.insert(key).second) {
                edits.push_back({im.start, im.end, ""});
                report_.fix(im.line, "removed a duplicate import of " + im.path + ".");
            }
        }
        apply(edits);
    }

    void imports() {
        duplicateImports();
        std::vector<Tok> toks = tokens();
        std::vector<Import> list = importList(toks);
        std::vector<Edit> edits;
        std::set<std::string> imported;
        for (const Import& im : list) {
            std::string name = im.alias.empty() ? simpleName(im.path) : im.alias;
            imported.insert(name);
            if (im.path.empty() || validPath(im.path) || kBlacklisted.count(im.path)) {
                if (kBlacklisted.count(im.path)) report_.problem(im.line, im.path + " is blocked by Polymod in V-Slice scripts.");
                continue;
            }
            std::string first = im.path.substr(0, im.path.find('.'));
            std::string simple = simpleName(im.path);
            auto type = types_.find(simple);
            bool fromPsych = kPsychPackages.count(first) > 0 || (im.path.find('.') == std::string::npos && !kHaxeStd.count(im.path) && psychClass(im.path));
            if (type != types_.end() && std::string(type->second->kind) != "typedef" && (fromPsych || first == "funkin" || first == "flixel")) {
                std::string text = src_.substr(im.start, im.end - im.start);
                size_t at = text.find(im.path);
                if (at != std::string::npos) {
                    edits.push_back({im.start + at, im.start + at + im.path.size(), type->second->path});
                    report_.fix(im.line, "import " + im.path + " doesn't exist in V-Slice; it is " + type->second->path + ".");
                }
                continue;
            }
            if (fromPsych && type == types_.end()) {
                if (simple == "ClientPrefs") {
                    edits.push_back({im.start, im.end, ""});
                    report_.fix(im.line, "removed import " + im.path + "; V-Slice uses Preferences, which is always imported.");
                } else if (simple == "Paths") {
                    edits.push_back({im.start, im.end, ""});
                    report_.fix(im.line, "removed import " + im.path + "; V-Slice always imports Paths.");
                } else {
                    report_.problem(im.line, "import " + im.path + " is a Psych Engine class with no V-Slice equivalent.");
                }
            }
        }
        std::set<std::string> declared;
        std::set<std::string> used;
        auto typeOnly = [&](int i) {
            int p = prevSig(toks, i);
            if (p < 0) return false;
            const std::string& prev = toks[static_cast<size_t>(p)].t;
            if (prev == "<" || prev == "->") return true;
            if (prev == ",") {
                int depth = 0;
                for (int j = p - 1; j >= 0; j--) {
                    const std::string& w = toks[static_cast<size_t>(j)].t;
                    if (w == ">") depth++;
                    else if (w == "<") {
                        if (depth == 0) return true;
                        depth--;
                    } else if (w == "(" || w == "{" || w == ";" || w == "=") return false;
                }
                return false;
            }
            if (prev != ":") return false;
            int before = prevSig(toks, p);
            if (before < 0) return false;
            const std::string& b = toks[static_cast<size_t>(before)].t;
            if (b == ")") return true;
            int owner = prevSig(toks, before);
            if (owner >= 0) {
                const std::string& o = toks[static_cast<size_t>(owner)].t;
                if (o == "var" || o == "final" || o == "?") return true;
            }
            int depth = 0;
            for (int j = before - 1; j >= 0; j--) {
                const std::string& w = toks[static_cast<size_t>(j)].t;
                if (w == ")" || w == "}" || w == "]") depth++;
                else if (w == "(" || w == "{" || w == "[") {
                    if (depth == 0) return w == "(";
                    depth--;
                }
            }
            return false;
        };
        std::map<std::string, int> usedLine;
        for (size_t i = 0; i < toks.size(); i++) {
            const Tok& t = toks[i];
            if (t.k != TK::Name || t.t.empty() || !std::isupper(static_cast<unsigned char>(t.t[0]))) continue;
            bool hasLower = false;
            for (char c : t.t) if (std::islower(static_cast<unsigned char>(c))) hasLower = true;
            if (!hasLower) continue;
            int ii = static_cast<int>(i);
            int p = prevSig(toks, ii);
            int n = nextSig(toks, ii);
            std::string prev = p >= 0 ? toks[static_cast<size_t>(p)].t : "";
            std::string next = n >= 0 ? toks[static_cast<size_t>(n)].t : "";
            if (prev == "class" || prev == "typedef" || prev == "enum" || prev == "interface" || prev == "abstract") {
                declared.insert(t.t);
                continue;
            }
            if (prev == "." || prev == "?.") continue;
            if (next == ":" && (prev == "{" || prev == ",")) continue;
            bool inImport = false;
            for (const Import& im : list) if (t.s >= im.start && t.e <= im.end) inImport = true;
            if (inImport) continue;
            if (typeOnly(ii)) continue;
            used.insert(t.t);
            if (!usedLine.count(t.t)) usedLine[t.t] = t.line;
        }
        std::vector<std::string> add;
        for (const std::string& name : used) {
            if (imported.count(name) || declared.count(name) || kHaxeStd.count(name)) continue;
            auto type = types_.find(name);
            if (type == types_.end() || std::string(type->second->kind) == "typedef") continue;
            std::string path = type->second->path;
            if (kBlacklisted.count(path)) continue;
            add.push_back(path);
            report_.fix(usedLine[name], name + " was used without an import; added import " + path + ".");
        }
        if (!add.empty()) {
            std::sort(add.begin(), add.end());
            std::string block;
            for (const std::string& p : add) block += "import " + p + ";\n";
            size_t at = 0;
            if (!list.empty()) {
                at = list.back().end;
                if (at > 0 && src_[at - 1] != '\n') block = "\n" + block;
            } else {
                for (size_t i = 0; i < toks.size(); i++) {
                    if (toks[i].k == TK::Name && toks[i].t == "package") {
                        size_t semi = src_.find(';', toks[i].e);
                        if (semi != std::string::npos) {
                            at = semi + 1;
                            if (at < src_.size() && src_[at] == '\n') at++;
                            block = "\n" + block;
                        }
                        break;
                    }
                    if (significant(toks[i])) break;
                }
                if (at == 0) block += "\n";
            }
            edits.push_back({at, at, block});
        }
        apply(edits);
    }

    struct Function {
        size_t modsStart;
        size_t functionTok;
        bool hasOverride;
        std::string name;
        int nameTok;
        int openParen;
        int closeParen;
        int openBrace;
        int closeBrace;
        int line;
    };

    void structure() {
        std::vector<Tok> toks = tokens();
        int classTok = -1;
        std::string base;
        std::string className;
        for (size_t i = 0; i < toks.size(); i++) {
            if (toks[i].k == TK::Name && toks[i].t == "class") {
                int n = nextSig(toks, static_cast<int>(i));
                if (n < 0) break;
                className = toks[static_cast<size_t>(n)].t;
                for (int j = nextSig(toks, n); j >= 0 && toks[static_cast<size_t>(j)].t != "{"; j = nextSig(toks, j)) {
                    if (toks[static_cast<size_t>(j)].t == "extends") {
                        int b = nextSig(toks, j);
                        std::string full;
                        while (b >= 0 && (toks[static_cast<size_t>(b)].k == TK::Name || toks[static_cast<size_t>(b)].t == ".")) {
                            full += toks[static_cast<size_t>(b)].t;
                            b = nextSig(toks, b);
                        }
                        base = simpleName(full);
                    }
                }
                classTok = static_cast<int>(i);
                break;
            }
        }
        if (classTok < 0 || base.empty()) return;
        auto baseIt = bases_.find(base);
        if (baseIt == bases_.end()) return;
        std::set<std::string> callbacks;
        {
            std::string csv = baseIt->second;
            size_t start = 0;
            while (start <= csv.size()) {
                size_t comma = csv.find(',', start);
                callbacks.insert(csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
        std::vector<std::string> callbackList(callbacks.begin(), callbacks.end());
        int classOpen = -1;
        for (int j = classTok; j >= 0 && static_cast<size_t>(j) < toks.size(); j = nextSig(toks, j)) {
            if (toks[static_cast<size_t>(j)].t == "{") {
                classOpen = j;
                break;
            }
        }
        if (classOpen < 0) return;
        int classClose = matchClose(toks, classOpen, "{", "}");
        if (classClose < 0) return;
        std::vector<Function> functions;
        int depth = 0;
        for (int j = classOpen; j <= classClose; j++) {
            const Tok& t = toks[static_cast<size_t>(j)];
            if (t.k == TK::Op && t.t == "{") depth++;
            if (t.k == TK::Op && t.t == "}") depth--;
            if (depth != 1 || t.k != TK::Name || t.t != "function") continue;
            Function f{};
            f.functionTok = static_cast<size_t>(j);
            f.modsStart = t.s;
            f.hasOverride = false;
            for (int p = prevSig(toks, j); p > classOpen; p = prevSig(toks, p)) {
                const std::string& w = toks[static_cast<size_t>(p)].t;
                if (w == "override") f.hasOverride = true;
                else if (w == "public" || w == "private" || w == "static" || w == "inline" || w == "dynamic") {
                } else break;
                f.modsStart = toks[static_cast<size_t>(p)].s;
            }
            f.nameTok = nextSig(toks, j);
            if (f.nameTok < 0) continue;
            f.name = toks[static_cast<size_t>(f.nameTok)].t;
            f.line = toks[static_cast<size_t>(f.nameTok)].line;
            f.openParen = nextSig(toks, f.nameTok);
            if (f.openParen < 0 || toks[static_cast<size_t>(f.openParen)].t != "(") continue;
            f.closeParen = matchClose(toks, f.openParen, "(", ")");
            if (f.closeParen < 0) continue;
            int b = nextSig(toks, f.closeParen);
            while (b >= 0 && toks[static_cast<size_t>(b)].t != "{" && toks[static_cast<size_t>(b)].t != ";") b = nextSig(toks, b);
            if (b < 0 || toks[static_cast<size_t>(b)].t != "{") continue;
            f.openBrace = b;
            f.closeBrace = matchClose(toks, b, "{", "}");
            if (f.closeBrace < 0) continue;
            functions.push_back(f);
            j = f.closeBrace - 1;
            depth = 2;
        }
        std::vector<Edit> edits;
        bool hasCtor = false;
        std::set<std::string> defined;
        for (const Function& f : functions) defined.insert(f.name);
        for (const Function& f : functions) {
            if (f.name == "new") {
                hasCtor = true;
                continue;
            }
            std::string name = f.name;
            if (!callbacks.count(name)) {
                if (name.size() >= 6 && name.compare(0, 2, "on") == 0) {
                    std::string fixed = nearest(name, callbackList);
                    if (!fixed.empty() && fixed != name && !defined.count(fixed)) {
                        edits.push_back({toks[static_cast<size_t>(f.nameTok)].s, toks[static_cast<size_t>(f.nameTok)].e, fixed});
                        report_.fix(f.line, name + " is never called by V-Slice; renamed it to the callback " + fixed + ".");
                    }
                }
                continue;
            }
            std::string type = events_.count(name) ? events_[name] : "ScriptEvent";
            std::vector<std::pair<std::string, std::string>> params;
            {
                std::string text = src_.substr(toks[static_cast<size_t>(f.openParen)].e, toks[static_cast<size_t>(f.closeParen)].s - toks[static_cast<size_t>(f.openParen)].e);
                int d = 0;
                std::string cur;
                auto flush = [&]() {
                    std::string p = trim(cur);
                    cur.clear();
                    if (p.empty()) return;
                    if (p[0] == '?') p = p.substr(1);
                    size_t colon = p.find(':');
                    std::string pname = trim(p.substr(0, colon));
                    std::string ptype = colon == std::string::npos ? "" : trim(p.substr(colon + 1));
                    size_t eq = ptype.find('=');
                    if (eq != std::string::npos) ptype = trim(ptype.substr(0, eq));
                    params.push_back({pname, ptype});
                };
                for (char c : text) {
                    if (c == '<' || c == '(' || c == '{') d++;
                    if (c == '>' || c == ')' || c == '}') d--;
                    if (c == ',' && d == 0) flush();
                    else cur += c;
                }
                flush();
            }
            std::string eventName = "event";
            bool rewrite = false;
            std::string oldName;
            std::string replacement;
            if (params.empty()) {
                eventName = "";
            } else if (params.size() == 1 && (params[0].second.empty() || params[0].second == "Dynamic" || params[0].second.find("Event") != std::string::npos)) {
                eventName = params[0].first;
            } else {
                rewrite = true;
                if (params.size() == 1) {
                    oldName = params[0].first;
                    std::string lname = lower(oldName);
                    if (name == "onUpdate" && (params[0].second == "Float" || lname.find("elapsed") != std::string::npos || lname == "dt")) replacement = "event.elapsed";
                    if (name == "onBeatHit" && (params[0].second == "Int" || lname.find("beat") != std::string::npos)) replacement = "event.beat";
                    if (name == "onStepHit" && (params[0].second == "Int" || lname.find("step") != std::string::npos)) replacement = "event.step";
                }
            }
            if (rewrite) {
                edits.push_back({toks[static_cast<size_t>(f.openParen)].e, toks[static_cast<size_t>(f.closeParen)].s, eventName + ":" + type});
                report_.fix(f.line, name + " receives a " + type + " in V-Slice; fixed its parameter.");
                if (!oldName.empty()) {
                    if (replacement.empty()) {
                        report_.problem(f.line, "\"" + oldName + "\" in " + name + " has no V-Slice equivalent; read it from the event.");
                    } else {
                        for (int j = f.openBrace + 1; j < f.closeBrace; j++) {
                            const Tok& t = toks[static_cast<size_t>(j)];
                            if (t.k != TK::Name || t.t != oldName) continue;
                            int p = prevSig(toks, j);
                            if (p >= 0 && (toks[static_cast<size_t>(p)].t == "." || toks[static_cast<size_t>(p)].t == "?.")) continue;
                            edits.push_back({t.s, t.e, replacement});
                        }
                        report_.fix(f.line, "\"" + oldName + "\" is " + replacement + " in V-Slice.");
                    }
                }
            }
            bool callsSuper = false;
            for (int j = f.openBrace + 1; j < f.closeBrace; j++) {
                if (toks[static_cast<size_t>(j)].k == TK::Name && toks[static_cast<size_t>(j)].t == "super") {
                    int n = nextSig(toks, j);
                    int m = n >= 0 ? nextSig(toks, n) : -1;
                    if (n >= 0 && toks[static_cast<size_t>(n)].t == "." && m >= 0 && toks[static_cast<size_t>(m)].t == name) callsSuper = true;
                }
            }
            bool needsSuper = base == "Stage" || (base.size() > 9 && base.compare(base.size() - 9, 9, "Character") == 0);
            if (!callsSuper && name == "onCreate" && needsSuper && !eventName.empty()) {
                std::string indent = indentAfter(toks[static_cast<size_t>(f.openBrace)].e, f.modsStart);
                edits.push_back({toks[static_cast<size_t>(f.openBrace)].e, toks[static_cast<size_t>(f.openBrace)].e, "\n" + indent + "super." + name + "(" + eventName + ");"});
                report_.fix(f.line, name + " didn't call super." + name + "(); added it so " + base + " keeps working.");
            }
        }
        static const std::set<std::string> noCtor = {"BaseCharacter", "Bopper", "StageProp"};
        if (!hasCtor && !noCtor.count(base)) {
            std::string id = opt_.id.empty() ? baseNameOf(opt_.fileName) : opt_.id;
            if (id.empty()) id = className;
            std::string indent = indentAfter(toks[static_cast<size_t>(classOpen)].e, toks[static_cast<size_t>(classTok)].s);
            std::string inner = indent + "  ";
            edits.push_back({toks[static_cast<size_t>(classOpen)].e, toks[static_cast<size_t>(classOpen)].e,
                "\n" + indent + "public function new()\n" + indent + "{\n" + inner + "super(\"" + id + "\");\n" + indent + "}\n"});
            report_.fix(toks[static_cast<size_t>(classTok)].line, className + " had no constructor; added new() with super(\"" + id + "\").");
        }
        apply(edits);
    }

    static std::string baseNameOf(const std::string& file) {
        std::string name = file;
        size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        size_t dot = name.find('.');
        return dot == std::string::npos ? name : name.substr(0, dot);
    }

    std::string indentAfter(size_t pos, size_t headerStart) {
        size_t lineStart = src_.rfind('\n', headerStart == 0 ? 0 : headerStart - 1);
        lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
        std::string base;
        for (size_t i = lineStart; i < src_.size() && (src_[i] == ' ' || src_[i] == '\t'); i++) base += src_[i];
        size_t next = src_.find('\n', pos);
        if (next != std::string::npos) {
            std::string inner;
            for (size_t i = next + 1; i < src_.size() && (src_[i] == ' ' || src_[i] == '\t'); i++) inner += src_[i];
            if (inner.size() > base.size()) return inner;
        }
        return base + "  ";
    }

    void psychIsms() {
        std::vector<Tok> toks = tokens();
        std::vector<Edit> edits;
        bool gameDeclared = false;
        for (size_t i = 0; i < toks.size(); i++) {
            if (toks[i].k == TK::Name && toks[i].t == "game") {
                int p = prevSig(toks, static_cast<int>(i));
                if (p >= 0 && (toks[static_cast<size_t>(p)].t == "var" || toks[static_cast<size_t>(p)].t == "final" || toks[static_cast<size_t>(p)].t == "(" || toks[static_cast<size_t>(p)].t == ",")) gameDeclared = true;
            }
        }
        static const std::map<std::string, std::string> characters = {{"boyfriend", "getBoyfriend()"}, {"bf", "getBoyfriend()"}, {"dad", "getDad()"},
            {"gf", "getGirlfriend()"}, {"girlfriend", "getGirlfriend()"}};
        static const std::map<std::string, std::string> conductor = {{"songPosition", "songPosition"}, {"crochet", "beatLengthMs"},
            {"stepCrochet", "stepLengthMs"}, {"bpm", "bpm"}, {"curBeat", "currentBeat"}, {"curStep", "currentStep"}};
        static const std::map<std::string, std::string> prefs = {{"downScroll", "downscroll"}, {"flashing", "flashingLights"},
            {"camZooms", "zoomCamera"}, {"framerate", "framerateCap"}, {"showFPS", "debugDisplay"}};
        for (size_t i = 0; i < toks.size(); i++) {
            const Tok& t = toks[i];
            if (t.k != TK::Name) continue;
            int ii = static_cast<int>(i);
            int p = prevSig(toks, ii);
            int n = nextSig(toks, ii);
            std::string prev = p >= 0 ? toks[static_cast<size_t>(p)].t : "";
            std::string next = n >= 0 ? toks[static_cast<size_t>(n)].t : "";
            if (t.t == "game" && !gameDeclared && prev != "." && next == ".") {
                edits.push_back({t.s, t.e, "PlayState.instance"});
                report_.fix(t.line, "\"game\" is Psych's PlayState; V-Slice scripts use PlayState.instance.");
                continue;
            }
            if (prev == "." && characters.count(t.t) && next != "(") {
                int owner = prevSig(toks, p);
                bool fromGame = false;
                if (owner >= 0 && toks[static_cast<size_t>(owner)].t == "game" && !gameDeclared) fromGame = true;
                if (owner >= 0 && toks[static_cast<size_t>(owner)].t == "instance") {
                    int dot = prevSig(toks, owner);
                    int cls = dot >= 0 ? prevSig(toks, dot) : -1;
                    fromGame = cls >= 0 && toks[static_cast<size_t>(cls)].t == "PlayState";
                }
                if (fromGame) {
                    edits.push_back({t.s, t.e, "currentStage." + characters.at(t.t)});
                    report_.fix(t.line, "V-Slice keeps the characters on the stage: PlayState.instance.currentStage." + characters.at(t.t) + ".");
                }
                continue;
            }
            if (prev == "." && t.t == "playAnim" && next == "(") {
                edits.push_back({t.s, t.e, "playAnimation"});
                report_.fix(t.line, "playAnim() is Psych's; V-Slice sprites and characters use playAnimation().");
                continue;
            }
            if (t.t == "Conductor" && prev != "." && next == ".") {
                int m = nextSig(toks, n);
                if (m >= 0 && conductor.count(toks[static_cast<size_t>(m)].t)) {
                    const std::string& field = toks[static_cast<size_t>(m)].t;
                    edits.push_back({t.s, toks[static_cast<size_t>(m)].e, "Conductor.instance." + conductor.at(field)});
                    report_.fix(t.line, "Conductor." + field + " is Conductor.instance." + conductor.at(field) + " in V-Slice.");
                }
                continue;
            }
            if (t.t == "ClientPrefs" && prev != "." && next == ".") {
                int m = nextSig(toks, n);
                if (m >= 0 && toks[static_cast<size_t>(m)].t == "data") {
                    int dot = nextSig(toks, m);
                    int f = dot >= 0 ? nextSig(toks, dot) : -1;
                    if (f >= 0 && toks[static_cast<size_t>(dot)].t == "." && prefs.count(toks[static_cast<size_t>(f)].t)) {
                        const std::string& field = toks[static_cast<size_t>(f)].t;
                        edits.push_back({t.s, toks[static_cast<size_t>(f)].e, "Preferences." + prefs.at(field)});
                        report_.fix(t.line, "ClientPrefs.data." + field + " is Preferences." + prefs.at(field) + " in V-Slice.");
                    } else {
                        report_.problem(t.line, "ClientPrefs is Psych's; V-Slice options are in Preferences.");
                    }
                }
                continue;
            }
            if (t.t == "Paths" && next == "." && prev == ",") {
                int m = nextSig(toks, n);
                int open = m >= 0 ? nextSig(toks, m) : -1;
                if (m >= 0 && toks[static_cast<size_t>(m)].t == "image" && open >= 0 && toks[static_cast<size_t>(open)].t == "(") {
                    int close = matchClose(toks, open, "(", ")");
                    bool insideCreate = false;
                    int depth = 0;
                    for (int j = p; j >= 0; j--) {
                        const std::string& w = toks[static_cast<size_t>(j)].t;
                        if (w == ")") depth++;
                        if (w == "(") {
                            if (depth == 0) {
                                int fn = prevSig(toks, j);
                                if (fn >= 0) {
                                    const std::string& f = toks[static_cast<size_t>(fn)].t;
                                    insideCreate = f == "create" || f == "createSparrow" || f == "createPacker" || f == "createTextureAtlas";
                                }
                                break;
                            }
                            depth--;
                        }
                    }
                    if (insideCreate && close > open) {
                        std::string inner = trim(src_.substr(toks[static_cast<size_t>(open)].e, toks[static_cast<size_t>(close)].s - toks[static_cast<size_t>(open)].e));
                        edits.push_back({t.s, toks[static_cast<size_t>(close)].e, inner});
                        report_.fix(t.line, "FunkinSprite.create() takes the image key, not Paths.image(); removed Paths.image().");
                    }
                }
                continue;
            }
            if (t.t == "save" && prev == "." && p > 0 && toks[static_cast<size_t>(prevSig(toks, p))].t == "FlxG") {
                report_.problem(t.line, "FlxG.save is blocked by Polymod in V-Slice scripts.");
                continue;
            }
            if (t.t == "Sys" && prev != "." && next == ".") {
                report_.problem(t.line, "Sys is blocked by Polymod in V-Slice scripts.");
            }
        }
        apply(edits);
    }
};

}

DoctorResult fixLuaScript(const std::string& source, const DoctorOptions& options) {
    LuaDoctor doctor(source, options);
    DoctorResult r = doctor.run();
    r.changed = r.source != source;
    return r;
}

DoctorResult fixHaxeScript(const std::string& source, const DoctorOptions& options) {
    HaxeDoctor doctor(source, options);
    DoctorResult r = doctor.run();
    r.changed = r.source != source;
    return r;
}

}
