#include "vscript/VScript.h"

#include "Converter.hpp"
#include "Funkin.hpp"
#include "Generator.hpp"
#include "HaxeLua.hpp"
#include "Doctor.hpp"
#include "Shader.hpp"
#include "Image.hpp"
#include "Reverse.hpp"
#include "Json.hpp"

#include <cstdlib>
#include <cstring>

namespace {

char* copy(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (out == nullptr) return nullptr;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

vscript::Base baseOf(const std::string& text) {
    if (text == "song") return vscript::Base::Song;
    if (text == "stage") return vscript::Base::Stage;
    if (text == "notekind") return vscript::Base::NoteKind;
    if (text == "event") return vscript::Base::SongEvent;
    return vscript::Base::Module;
}

}

extern "C" {

const char* vscript_version(void) {
    return VSCRIPT_VERSION_STRING;
}

char* vscript_convert_lua(const char* source, const char* options_json) {
    using namespace vscript;
    bool ok = false;
    Json options = Json::parse(options_json ? options_json : "{}", ok);
    if (!ok) options = Json::object();
    ConvertOptions o;
    o.base = baseOf(options["base"].text("module"));
    o.className = options["className"].text("Script");
    o.id = options["id"].text("script");
    o.scopeKind = options["scopeKind"].text();
    o.scopeId = options["scopeId"].text();
    o.priority = static_cast<int>(options["priority"].asNumber(1000));
    o.eventName = options["eventName"].text();
    o.title = options["title"].text();
    o.noteStyle = options["noteStyle"].text();
    o.behindLayer = static_cast<int>(options["behindLayer"].asNumber(99));
    o.frontLayer = static_cast<int>(options["frontLayer"].asNumber(1000));
    for (const auto& field : options["events"].fields()) o.events[field.first] = field.second.text();
    for (const auto& field : options["noteKinds"].fields()) o.noteKinds[field.first] = field.second.text();
    for (const Json& prop : options["stageProps"].items()) o.stageProps.insert(prop.text());
    for (const Json& line : options["skipLines"].items()) o.skipLines.insert(static_cast<int>(line.asNumber()));
    ConvertResult result = convertLua(source ? source : "", o);
    Json out = Json::object();
    out.set("ok", Json::boolean(result.ok));
    out.set("source", Json::string(result.source));
    Json callbacks = Json::array();
    for (const std::string& c : result.callbacks) callbacks.push(Json::string(c));
    out.set("callbacks", callbacks);
    Json issues = Json::array();
    for (const Issue& i : result.issues) {
        Json item = Json::object();
        item.set("line", Json::number(i.line));
        item.set("message", Json::string(i.message));
        issues.push(item);
    }
    out.set("issues", issues);
    Json api = Json::array();
    for (const std::string& a : result.apiCalls) api.push(Json::string(a));
    out.set("api", api);
    return copy(out.dump());
}

char* vscript_convert_hx(const char* source, const char* options_json) {
    using namespace vscript;
    bool ok = false;
    Json options = Json::parse(options_json ? options_json : "{}", ok);
    if (!ok) options = Json::object();
    HaxeLuaOptions o;
    o.engine = options["engine"].text("vslice");
    o.fileName = options["fileName"].text();
    o.id = options["id"].text();
    o.eventName = options["eventName"].text();
    o.noteType = options["noteType"].text();
    o.characterLayer = static_cast<int>(options["characterLayer"].asNumber(300));
    for (const Json& prop : options["stageProps"].items()) o.stageProps.insert(prop.text());
    HaxeLuaResult result = convertHaxeToLua(source ? source : "", o);
    Json out = Json::object();
    out.set("ok", Json::boolean(result.ok));
    out.set("source", Json::string(result.source));
    out.set("kind", Json::string(result.kind));
    out.set("className", Json::string(result.className));
    out.set("targetId", Json::string(result.targetId));
    out.set("title", Json::string(result.title));
    out.set("fallbacks", Json::number(result.fallbacks));
    Json schema = Json::array();
    for (const std::string& s : result.schema) schema.push(Json::string(s));
    out.set("schema", schema);
    Json callbacks = Json::array();
    for (const std::string& c : result.callbacks) callbacks.push(Json::string(c));
    out.set("callbacks", callbacks);
    Json issues = Json::array();
    for (const Issue& i : result.issues) {
        Json item = Json::object();
        item.set("line", Json::number(i.line));
        item.set("message", Json::string(i.message));
        issues.push(item);
    }
    out.set("issues", issues);
    return copy(out.dump());
}

static char* doctorJson(const vscript::DoctorResult& result) {
    using namespace vscript;
    Json out = Json::object();
    out.set("source", Json::string(result.source));
    out.set("changed", Json::boolean(result.changed));
    auto list = [](const std::vector<Issue>& items) {
        Json array = Json::array();
        for (const Issue& i : items) {
            Json item = Json::object();
            item.set("line", Json::number(i.line));
            item.set("message", Json::string(i.message));
            array.push(item);
        }
        return array;
    };
    out.set("fixes", list(result.fixes));
    out.set("problems", list(result.problems));
    return copy(out.dump());
}

static vscript::DoctorOptions doctorOptions(const char* options_json) {
    using namespace vscript;
    bool ok = false;
    Json options = Json::parse(options_json ? options_json : "{}", ok);
    if (!ok) options = Json::object();
    DoctorOptions o;
    o.engine = options["engine"].text("vslice");
    o.fileName = options["fileName"].text();
    o.id = options["id"].text();
    return o;
}

char* vscript_convert_shader(const char* source, const char* options_json) {
    using namespace vscript;
    bool ok = false;
    Json options = Json::parse(options_json ? options_json : "{}", ok);
    if (!ok) options = Json::object();
    ShaderOptions o;
    o.from = options["from"].text();
    o.to = options["to"].text();
    o.name = options["name"].text();
    o.vertex = options["vertex"].asBool(false);
    ShaderResult result = convertShader(source ? source : "", o);
    Json out = Json::object();
    out.set("source", Json::string(result.source));
    out.set("changed", Json::boolean(result.changed));
    auto list = [](const std::vector<Issue>& items) {
        Json array = Json::array();
        for (const Issue& i : items) {
            Json item = Json::object();
            item.set("line", Json::number(i.line));
            item.set("message", Json::string(i.message));
            array.push(item);
        }
        return array;
    };
    out.set("fixes", list(result.fixes));
    out.set("problems", list(result.problems));
    Json uniforms = Json::array();
    for (const std::string& u : result.uniforms) uniforms.push(Json::string(u));
    out.set("uniforms", uniforms);
    return copy(out.dump());
}

char* vscript_fix_lua(const char* source, const char* options_json) {
    return doctorJson(vscript::fixLuaScript(source ? source : "", doctorOptions(options_json)));
}

char* vscript_fix_hx(const char* source, const char* options_json) {
    return doctorJson(vscript::fixHaxeScript(source ? source : "", doctorOptions(options_json)));
}

char* vscript_generate(const char* spec_json) {
    using namespace vscript;
    bool ok = false;
    Json spec = Json::parse(spec_json ? spec_json : "{}", ok);
    Json out = Json::object();
    if (!ok) {
        out.set("ok", Json::boolean(false));
        out.set("error", Json::string("The script spec is not valid JSON."));
        return copy(out.dump());
    }
    std::string error;
    std::string source = generateScript(spec, error);
    out.set("ok", Json::boolean(error.empty()));
    out.set("source", Json::string(source));
    if (!error.empty()) out.set("error", Json::string(error));
    return copy(out.dump());
}

char* vscript_builtin_event(const char* name, const char* class_name, const char* id) {
    using namespace vscript;
    bool known = false;
    std::string source = builtinEvent(name ? name : "", class_name ? class_name : "Event", id ? id : "event", known);
    Json out = Json::object();
    out.set("known", Json::boolean(known));
    out.set("source", Json::string(source));
    return copy(out.dump());
}

char* vscript_convert_data(const char* kind, const char* input_json) {
    using namespace vscript;
    bool ok = false;
    Json input = Json::parse(input_json ? input_json : "", ok);
    Json out = Json::object();
    if (!ok) {
        out.set("ok", Json::boolean(false));
        out.set("error", Json::string("The input could not be read."));
        return copy(out.dump());
    }
    std::string type = kind ? kind : "";
    std::string error;
    Json result;
    if (type == "chart") {
        result = convertPsychCharts(input, error);
        if (error.empty() && input["options"]["render"].asBool(false)) result = renderPsychCharts(result);
    }
    else if (type == "character") result = convertPsychCharacter(input, error);
    else if (type == "stage") result = convertPsychStage(input, error);
    else error = "Unknown conversion \"" + type + "\".";
    out.set("ok", Json::boolean(error.empty()));
    if (error.empty()) out.set("result", std::move(result));
    else out.set("error", Json::string(error));
    return copy(out.dump());
}

char* vscript_convert_charts(const char* const* difficulties, const char* const* texts, size_t count, const char* options_json, char*** files, size_t* file_count) {
    using namespace vscript;
    if (files) *files = nullptr;
    if (file_count) *file_count = 0;
    Json input = Json::object();
    Json charts = Json::array();
    for (size_t i = 0; i < count; i++) {
        Json chart = Json::object();
        chart.set("difficulty", Json::string(difficulties && difficulties[i] ? difficulties[i] : ""));
        chart.set("text", Json::string(texts && texts[i] ? texts[i] : ""));
        charts.push(std::move(chart));
    }
    input.set("charts", std::move(charts));
    bool ok = false;
    Json options = Json::parse(options_json && *options_json ? options_json : "{}", ok);
    input.set("options", ok ? std::move(options) : Json::object());
    std::string error;
    Json result = convertPsychCharts(input, error);
    Json out = Json::object();
    out.set("ok", Json::boolean(error.empty()));
    if (!error.empty()) {
        out.set("error", Json::string(error));
        return copy(out.dump());
    }
    Json rendered = renderPsychCharts(result);
    const auto& list = rendered["files"].items();
    Json names = Json::array();
    char** texts_out = static_cast<char**>(std::calloc(list.size() > 0 ? list.size() : 1, sizeof(char*)));
    for (size_t i = 0; i < list.size(); i++) {
        names.push(Json::string(list[i].items()[0].text()));
        if (texts_out) texts_out[i] = copy(list[i].items()[1].text());
    }
    out.set("report", rendered["report"]);
    out.set("variations", rendered["variations"]);
    out.set("files", std::move(names));
    if (files) *files = texts_out;
    else if (texts_out) vscript_free_files(texts_out, list.size());
    if (file_count) *file_count = list.size();
    return copy(out.dump());
}

char* vscript_convert_files(const char* kind, const char* const* names, const char* const* texts, size_t count, const char* options_json, char*** files, size_t* file_count) {
    using namespace vscript;
    if (files) *files = nullptr;
    if (file_count) *file_count = 0;
    NamedTexts inputs;
    inputs.reserve(count);
    for (size_t i = 0; i < count; i++) inputs.emplace_back(names && names[i] ? names[i] : "", texts && texts[i] ? texts[i] : "");
    bool ok = false;
    Json options = Json::parse(options_json && *options_json ? options_json : "{}", ok);
    if (!ok) options = Json::object();
    NamedTexts outputs;
    std::string error;
    Json result = convertFiles(kind ? kind : "", inputs, options, outputs, error);
    Json out = Json::object();
    out.set("ok", Json::boolean(error.empty()));
    if (!error.empty()) {
        out.set("error", Json::string(error));
        return copy(out.dump());
    }
    out.set("result", std::move(result));
    Json paths = Json::array();
    char** texts_out = static_cast<char**>(std::calloc(outputs.empty() ? 1 : outputs.size(), sizeof(char*)));
    for (size_t i = 0; i < outputs.size(); i++) {
        paths.push(Json::string(outputs[i].first));
        if (texts_out) texts_out[i] = copy(outputs[i].second);
    }
    out.set("files", std::move(paths));
    if (files) *files = texts_out;
    else if (texts_out) vscript_free_files(texts_out, outputs.size());
    if (file_count) *file_count = outputs.size();
    return copy(out.dump());
}

void vscript_free_files(char** files, size_t file_count) {
    if (!files) return;
    for (size_t i = 0; i < file_count; i++) std::free(files[i]);
    std::free(files);
}

namespace {

unsigned char* copyBytes(const std::vector<uint8_t>& bytes) {
    unsigned char* out = static_cast<unsigned char*>(std::malloc(bytes.empty() ? 1 : bytes.size()));
    if (out && !bytes.empty()) std::memcpy(out, bytes.data(), bytes.size());
    return out;
}

vscript::Image wrap(const unsigned char* rgba, int width, int height) {
    vscript::Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(rgba, rgba + size_t(width) * height * 4);
    return image;
}

}

unsigned char* vscript_png_decode(const unsigned char* data, size_t size, int* width, int* height) {
    if (!data || !width || !height) return nullptr;
    vscript::Image image;
    std::string error;
    if (!vscript::decodePng(data, size, image, error)) return nullptr;
    *width = image.width;
    *height = image.height;
    return copyBytes(image.rgba);
}

unsigned char* vscript_png_encode(const unsigned char* rgba, int width, int height, int level, size_t* size) {
    if (!rgba || width <= 0 || height <= 0 || !size) return nullptr;
    std::vector<uint8_t> png = vscript::encodePng(wrap(rgba, width, height), level);
    *size = png.size();
    return copyBytes(png);
}

unsigned char* vscript_png_optimize(const unsigned char* data, size_t size, int level, size_t* out_size) {
    if (!data || !out_size) return nullptr;
    std::vector<uint8_t> png;
    if (!vscript::optimizePng(data, size, level, png)) return nullptr;
    *out_size = png.size();
    return copyBytes(png);
}

unsigned char* vscript_image_resize(const unsigned char* rgba, int width, int height, int new_width, int new_height, int smooth) {
    if (!rgba || width <= 0 || height <= 0 || new_width <= 0 || new_height <= 0) return nullptr;
    return copyBytes(vscript::resizeImage(wrap(rgba, width, height), new_width, new_height, smooth != 0).rgba);
}

void vscript_image_bake(unsigned char* rgba, int width, int height, unsigned int r, unsigned int g, unsigned int b) {
    if (!rgba || width <= 0 || height <= 0) return;
    vscript::Image image = wrap(rgba, width, height);
    vscript::bakeColors(image, r, g, b);
    std::memcpy(rgba, image.rgba.data(), image.rgba.size());
}

void vscript_free_bytes(unsigned char* data) {
    std::free(data);
}

void vscript_free(char* text) {
    std::free(text);
}

}
