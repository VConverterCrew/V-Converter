#include "Shader.hpp"

#include <cctype>
#include <set>

namespace vscript {

namespace {

bool identPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::vector<bool> codeMask(const std::string& src) {
    std::vector<bool> code(src.size(), true);
    size_t i = 0;
    while (i < src.size()) {
        if (src.compare(i, 2, "//") == 0) {
            while (i < src.size() && src[i] != '\n') code[i++] = false;
        } else if (src.compare(i, 2, "/*") == 0) {
            size_t end = src.find("*/", i + 2);
            end = end == std::string::npos ? src.size() : end + 2;
            while (i < end) code[i++] = false;
        } else {
            i++;
        }
    }
    return code;
}

bool wordAt(const std::string& src, const std::vector<bool>& code, size_t at, const std::string& word) {
    if (at + word.size() > src.size() || src.compare(at, word.size(), word) != 0 || !code[at]) return false;
    if (at > 0 && (identPart(src[at - 1]) || src[at - 1] == '.')) return false;
    size_t end = at + word.size();
    return end >= src.size() || !identPart(src[end]);
}

bool usesWord(const std::string& src, const std::string& word) {
    std::vector<bool> code = codeMask(src);
    for (size_t i = 0; i < src.size(); i++) if (wordAt(src, code, i, word)) return true;
    return false;
}

int replaceWord(std::string& src, const std::string& word, const std::string& with, bool needsCall = false) {
    std::vector<bool> code = codeMask(src);
    std::string out;
    int count = 0;
    for (size_t i = 0; i < src.size();) {
        if (wordAt(src, code, i, word)) {
            size_t j = i + word.size();
            while (needsCall && j < src.size() && (src[j] == ' ' || src[j] == '\t')) j++;
            if (!needsCall || (j < src.size() && src[j] == '(')) {
                out += with;
                i += word.size();
                count++;
                continue;
            }
        }
        out += src[i++];
    }
    src = out;
    return count;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> lines(const std::string& src) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t nl = src.find('\n', start);
        out.push_back(src.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

std::string join(const std::vector<std::string>& ls) {
    std::string out;
    for (size_t i = 0; i < ls.size(); i++) {
        if (i) out += "\n";
        out += ls[i];
    }
    return out;
}

bool declared(const std::string& src, const std::string& name) {
    for (const std::string& l : lines(src)) {
        std::string t = trim(l);
        if ((t.compare(0, 8, "uniform ") == 0 || t.compare(0, 8, "#define ") == 0 || t.compare(0, 6, "const ") == 0) && usesWord(t, name)) return true;
    }
    return false;
}

}

ShaderResult convertShader(const std::string& source, const ShaderOptions& opt) {
    ShaderResult r;
    std::string src = source;
    for (size_t i = 0; i < src.size(); i++) {
        if (src[i] == '\r' && i + 1 < src.size() && src[i + 1] == '\n') src.erase(i, 1);
    }
    if (src.size() >= 3 && static_cast<unsigned char>(src[0]) == 0xEF && static_cast<unsigned char>(src[1]) == 0xBB && static_cast<unsigned char>(src[2]) == 0xBF) src.erase(0, 3);
    auto fix = [&](int line, const std::string& message) { r.fixes.push_back({line, message}); };
    auto problem = [&](int line, const std::string& message) { r.problems.push_back({line, message}); };

    std::vector<std::string> ls = lines(src);
    std::vector<std::string> kept;
    bool header = false;
    for (size_t i = 0; i < ls.size(); i++) {
        std::string t = trim(ls[i]);
        if (t.compare(0, 8, "#version") == 0) {
            fix(static_cast<int>(i) + 1, "removed \"" + t + "\"; FlxRuntimeShader writes its own GLSL version for each platform.");
            continue;
        }
        if (t.compare(0, 7, "#pragma") == 0 && t.find("header") != std::string::npos) header = true;
        kept.push_back(ls[i]);
    }
    src = join(kept);

    if (opt.vertex) {
        std::vector<std::string> out;
        for (size_t i = 0; i < kept.size(); i++) {
            std::string t = trim(kept[i]);
            if (t.compare(0, 3, "in ") == 0) {
                out.push_back(kept[i].substr(0, kept[i].find("in ")) + "attribute " + t.substr(3));
                fix(static_cast<int>(i) + 1, "\"in\" is \"attribute\" in OpenGL ES 2 vertex shaders.");
            } else if (t.compare(0, 4, "out ") == 0) {
                out.push_back(kept[i].substr(0, kept[i].find("out ")) + "varying " + t.substr(4));
                fix(static_cast<int>(i) + 1, "\"out\" is \"varying\" in OpenGL ES 2 vertex shaders.");
            } else {
                out.push_back(kept[i]);
            }
        }
        src = join(out);
        if (!usesWord(src, "main")) problem(0, "this vertex shader has no main() function.");
        r.source = src;
        r.changed = r.source != source;
        return r;
    }

    bool shadertoy = usesWord(src, "mainImage") && !usesWord(src, "main");

    std::vector<std::string> body;
    std::string outName;
    ls = lines(src);
    for (size_t i = 0; i < ls.size(); i++) {
        std::string t = trim(ls[i]);
        if (t.compare(0, 4, "out ") == 0 && t.find("vec4") != std::string::npos && t.back() == ';') {
            size_t name = t.find_last_of(" \t", t.size() - 2);
            outName = t.substr(name + 1, t.size() - name - 2);
            fix(static_cast<int>(i) + 1, "\"" + t + "\" is gl_FragColor in OpenGL ES 2 (mobile).");
            continue;
        }
        if (t.compare(0, 3, "in ") == 0 && t.back() == ';') {
            body.push_back(ls[i].substr(0, ls[i].find("in ")) + "varying " + t.substr(3));
            fix(static_cast<int>(i) + 1, "\"in\" is \"varying\" in OpenGL ES 2 fragment shaders.");
            continue;
        }
        if (shadertoy && t.compare(0, 8, "uniform ") == 0 &&
            (usesWord(t, "iChannel0") || usesWord(t, "iResolution") || usesWord(t, "iTime") || usesWord(t, "iChannelResolution"))) {
            if (usesWord(t, "iChannel0")) {
                fix(static_cast<int>(i) + 1, "Shadertoy's iChannel0 is the sprite itself (bitmap).");
                continue;
            }
        }
        body.push_back(ls[i]);
    }
    src = join(body);
    if (!outName.empty() && outName != "gl_FragColor") replaceWord(src, outName, "gl_FragColor");

    int textures = replaceWord(src, "texture", "texture2D", true);
    if (textures > 0) fix(0, "texture() is texture2D() in OpenGL ES 2, which phones use (" + std::to_string(textures) + " call(s)).");

    if (shadertoy) {
        if (replaceWord(src, "iChannel0", "bitmap") > 0) fix(0, "Shadertoy's iChannel0 became bitmap, the sprite the shader is drawn on.");
        std::string defines;
        if (usesWord(src, "iResolution") && !declared(src, "iResolution")) defines += "#define iResolution vec3(openfl_TextureSize, 0.0)\n";
        if (usesWord(src, "iTime") && !declared(src, "iTime")) defines += "uniform float iTime;\n";
        if (usesWord(src, "iTimeDelta") && !declared(src, "iTimeDelta")) defines += "uniform float iTimeDelta;\n";
        if (usesWord(src, "iFrame") && !declared(src, "iFrame")) defines += "uniform int iFrame;\n";
        if (usesWord(src, "iMouse") && !declared(src, "iMouse")) defines += "uniform vec4 iMouse;\n";
        std::string top = header ? "" : "#pragma header\n";
        src = top + defines + "\n" + src;
        if (src.back() != '\n') src += "\n";
        src += "\nvoid main()\n{\n\tmainImage(gl_FragColor, openfl_TextureCoordv * openfl_TextureSize);\n}\n";
        header = true;
        fix(0, "converted the Shadertoy shader: mainImage() is called from main() with the sprite's pixel coordinates, and iTime is a uniform you set every frame.");
    } else if (!header) {
        bool flixel = usesWord(src, "openfl_TextureCoordv") || usesWord(src, "bitmap") || usesWord(src, "flixel_texture2D") || usesWord(src, "openfl_TextureSize");
        if (flixel) {
            src = "#pragma header\n" + src;
            header = true;
            fix(1, "added \"#pragma header\", which declares bitmap, openfl_TextureCoordv and flixel_texture2D.");
        }
    }

    if (header) {
        std::string before = src;
        std::vector<bool> code = codeMask(src);
        std::string out;
        int count = 0;
        for (size_t i = 0; i < src.size();) {
            if (wordAt(src, code, i, "texture2D")) {
                size_t j = i + 9;
                while (j < src.size() && (src[j] == ' ' || src[j] == '\t')) j++;
                if (j < src.size() && src[j] == '(') {
                    size_t k = j + 1;
                    while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) k++;
                    if (src.compare(k, 6, "bitmap") == 0 && (k + 6 >= src.size() || !identPart(src[k + 6]))) {
                        out += "flixel_texture2D";
                        i += 9;
                        count++;
                        continue;
                    }
                }
            }
            out += src[i++];
        }
        src = out;
        if (count > 0) fix(0, "texture2D(bitmap, ...) became flixel_texture2D(bitmap, ...) so the sprite's alpha and color transform still apply.");
    }

    if (!usesWord(src, "main")) problem(0, "this fragment shader has no main() function.");
    if (!usesWord(src, "gl_FragColor") && !shadertoy) problem(0, "this fragment shader never writes gl_FragColor.");

    std::set<std::string> seen;
    for (const std::string& l : lines(src)) {
        std::string t = trim(l);
        if (t.compare(0, 8, "uniform ") != 0) continue;
        size_t semi = t.find(';');
        std::string decl = t.substr(8, semi == std::string::npos ? std::string::npos : semi - 8);
        size_t space = decl.find_last_of(" \t");
        std::string name = space == std::string::npos ? decl : decl.substr(space + 1);
        size_t bracket = name.find('[');
        if (bracket != std::string::npos) name = name.substr(0, bracket);
        if (!name.empty() && seen.insert(name).second) r.uniforms.push_back(name);
    }
    r.source = src;
    r.changed = r.source != source;
    return r;
}

}
