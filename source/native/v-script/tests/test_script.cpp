#include "vscript/VScript.h"

#include "Converter.hpp"
#include "HaxeLua.hpp"
#include "Doctor.hpp"
#include "Haxe.hpp"
#include "Json.hpp"
#include "Lua.hpp"
#include "Shader.hpp"
#include "Image.hpp"

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

static bool has(const std::string& text, const std::string& part) {
    bool found = text.find(part) != std::string::npos;
    if (!found) std::fprintf(stderr, "missing: %s\n", part.c_str());
    return found;
}

static bool lacks(const std::string& text, const std::string& part) {
    bool found = text.find(part) != std::string::npos;
    if (found) std::fprintf(stderr, "unexpected: %s\n", part.c_str());
    return !found;
}

static int braceBalance(const std::string& text) {
    int depth = 0;
    bool quoted = false;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '"' && (i == 0 || text[i - 1] != '\\')) quoted = !quoted;
        if (quoted) continue;
        if (c == '{') depth++;
        if (c == '}') depth--;
        if (depth < 0) return -1;
    }
    return depth;
}

static vscript::ConvertResult convert(const std::string& lua, vscript::Base base, const std::string& className = "TestScript", const std::string& id = "test") {
    vscript::ConvertOptions o;
    o.base = base;
    o.className = className;
    o.id = id;
    o.noteKinds["Hurt Note"] = "hurt";
    return vscript::convertLua(lua, o);
}

static void testParser() {
    vscript::Block block = vscript::parseLua(
        "local a, b = 1, 'two'\n"
        "local s = [[long\nstring]] .. \"esc\\n\\65\\u{48}\"\n"
        "t = {1, 2, x = 3, ['y z'] = 4; 5}\n"
        "for i = 10, 1, -1 do if i % 2 == 0 then goto continue end ::continue:: end\n"
        "repeat a = a + 1 until a > 3\n"
        "function obj:method(...) return self, ... end\n"
        "local f = function(x) return x ^ 2 ^ 3 end\n"
        "print(#t, not a, -b, a ~= b, a != b)\n"
        "--[==[ comment ]==]\n"
        "call 'literal'\n"
        "call {1}\n");
    CHECK(block.size() == 10);
    bool threw = false;
    try {
        vscript::parseLua("function x( end");
    } catch (const vscript::LuaError& e) {
        threw = e.line == 1;
    }
    CHECK(threw);
}

static void testStage() {
    const char* lua =
        "local sway = 0\n"
        "function onCreate()\n"
        "  makeLuaSprite('bg', 'stageback', -600, -200)\n"
        "  setScrollFactor('bg', 0.9, 0.9)\n"
        "  addLuaSprite('bg', false)\n"
        "  makeAnimatedLuaSprite('crowd', 'crowd', 0, 300)\n"
        "  addAnimationByPrefix('crowd', 'bop', 'crowd bop', 24, false)\n"
        "  addAnimationByIndices('crowd', 'left', 'crowd bop', '0, 1, 2', 24)\n"
        "  addLuaSprite('crowd', true)\n"
        "end\n"
        "function onBeatHit()\n"
        "  if curBeat % 2 == 0 then playAnim('crowd', 'bop', true) end\n"
        "end\n"
        "function onUpdate(elapsed)\n"
        "  sway = sway + elapsed\n"
        "  setProperty('bg.angle', math.sin(sway) * 5)\n"
        "end\n";
    vscript::ConvertResult r = convert(lua, vscript::Base::Stage, "SpookyStage", "spooky");
    CHECK(r.ok);
    const std::string& s = r.source;
    CHECK(has(s, "class SpookyStage extends Stage"));
    CHECK(has(s, "import funkin.play.stage.Stage;"));
    CHECK(has(s, "import funkin.graphics.FunkinSprite;"));
    CHECK(has(s, "super(\"spooky\");"));
    CHECK(has(s, "var bg:FunkinSprite;"));
    CHECK(has(s, "public override function onCreate(event:ScriptEvent):Void"));
    CHECK(has(s, "bg = FunkinSprite.create(-600, -200, \"stageback\");"));
    CHECK(has(s, "behindSprites.push(bg);"));
    CHECK(has(s, "function layerSprites():Void"));
    CHECK(has(s, "var stage:Stage = this;"));
    CHECK(has(s, "public override function onCountdownStart(event:CountdownScriptEvent):Void"));
    CHECK(has(s, "layerSprites();"));
    CHECK(has(s, "bg?.scrollFactor?.set(0.9, 0.9);"));
    CHECK(has(s, "crowd = FunkinSprite.createSparrow(0, 300, \"crowd\");"));
    CHECK(has(s, "crowd?.animation?.addByIndices(\"left\", \"crowd bop\", [0, 1, 2], \"\", 24, false);"));
    CHECK(has(s, "frontSprites.push(crowd);"));
    CHECK(has(s, "add(crowd);"));
    CHECK(has(s, "stage.refresh();"));
    CHECK(has(s, "Conductor.instance.currentBeat % 2 == 0"));
    CHECK(has(s, "crowd?.animation?.play(\"bop\", true, false, 0);"));
    CHECK(has(s, "var elapsed:Float = event.elapsed;"));
    CHECK(has(s, "bg?.angle = Math.sin(sway) * 5;"));
    CHECK(has(s, "var sway:Dynamic = 0;"));
    CHECK(lacks(s, "psychCallback"));
    CHECK(lacks(s, "setProperty("));
    CHECK(lacks(s, "makeLuaSprite"));
    CHECK(braceBalance(s) == 0);
}

static void testSongEvents() {
    const char* lua =
        "function onEvent(name, v1, v2)\n"
        "  if name == 'Flash Camera' then\n"
        "    cameraFlash('hud', 'FF0000', tonumber(v1))\n"
        "  elseif name == 'Play Animation' then\n"
        "    debugPrint('anim', v1)\n"
        "  end\n"
        "end\n"
        "function goodNoteHit(id, dir, noteType, sustain)\n"
        "  if noteType == 'Hurt Note' then addHealth(-0.2) end\n"
        "end\n"
        "function opponentNoteHit(id, dir, noteType, sustain)\n"
        "  cameraShake('game', 0.01, 0.1)\n"
        "end\n"
        "function onEndSong()\n"
        "  if not seen then seen = true; return Function_Stop end\n"
        "  return Function_Continue\n"
        "end\n"
        "function onSectionHit()\n"
        "  triggerEvent('Hey!', 'bf', '')\n"
        "end\n";
    vscript::ConvertResult r = convert(lua, vscript::Base::Song, "BopeeboSong", "bopeebo");
    const std::string& s = r.source;
    CHECK(has(s, "class BopeeboSong extends Song"));
    CHECK(has(s, "public override function onSongEvent(event:SongEventScriptEvent):Void"));
    CHECK(has(s, "var name:String = event.eventData.eventKind;"));
    CHECK(has(s, "if (name == \"FlashCamera\")"));
    CHECK(has(s, "else if (name == \"PlayAnimation\")"));
    CHECK(has(s, "v1 = event.eventData.value.anim;"));
    CHECK(has(s, "PlayState.instance.camHUD.flash(0xFFFF0000, Std.parseFloat(v1), null, false);"));
    CHECK(has(s, "if (event.note.noteData.getMustHitNote())"));
    CHECK(has(s, "if (noteType == \"hurt\")"));
    CHECK(has(s, "PlayState.instance.health += -0.2;"));
    CHECK(has(s, "PlayState.instance.camGame.shake(0.01, 0.1);"));
    CHECK(has(s, "public override function onSongEnd(event:ScriptEvent):Void"));
    CHECK(has(s, "event.cancel();"));
    CHECK(has(s, "event.step % Conductor.instance.stepsPerMeasure == 0"));
    CHECK(has(s, "SongEventRegistry.handleEvent(new SongEventData(Conductor.instance.songPosition, \"PlayAnimation\", {anim: \"hey\", target: \"bf\", force: true}));"));
    CHECK(has(s, "import funkin.data.song.SongEventData;"));
    CHECK(braceBalance(s) == 0);
}

static void testModuleScope() {
    vscript::ConvertOptions o;
    o.base = vscript::Base::Module;
    o.className = "SpookyModule";
    o.id = "spooky-module";
    o.scopeKind = "song";
    o.scopeId = "spookeez";
    vscript::ConvertResult r = vscript::convertLua("function onUpdate(e) health = 1 end\nfunction onStepHit() if curStep == 4 then close() end end", o);
    const std::string& s = r.source;
    CHECK(has(s, "class SpookyModule extends Module"));
    CHECK(has(s, "super(\"spooky-module\", 1000);"));
    CHECK(has(s, "PlayState.instance.currentSong.id != \"spookeez\""));
    CHECK(has(s, "var closedGame:PlayState = null;"));
    CHECK(has(s, "closedGame = PlayState.instance;"));
    CHECK(has(s, "(closedGame != null && closedGame == PlayState.instance)"));
    CHECK(lacks(s, "activeGame != PlayState.instance) return;"));
    CHECK(braceBalance(s) == 0);
}

static void testLuaSemantics() {
    const char* lua =
        "local list = {}\n"
        "local names = {'a', 'b'}\n"
        "local cfg = {speed = 2, name = 'x'}\n"
        "function onCreate()\n"
        "  table.insert(list, 5)\n"
        "  for i, v in ipairs(names) do debugPrint(i .. v) end\n"
        "  for k, v in pairs(cfg) do debugPrint(k) end\n"
        "  for i = 1, #list do list[i] = list[i] * 2 end\n"
        "  for i = 0, 3 do local n = i end\n"
        "  local x = cfg.speed or 1\n"
        "  local y = x > 1 and 'fast' or 'slow'\n"
        "  local s = string.format('%d hits, %.2f%%', 10, 99.5)\n"
        "  local up = ('abc'):upper() .. string.sub('hello', 2, 3)\n"
        "  if string.find(up, 'B') then x = x ^ 2 end\n"
        "  if type(x) == 'number' and x then x = -x end\n"
        "  while x < 10 do x = x + 1 if x == 5 then break end end\n"
        "  repeat x = x - 1 until x <= 0\n"
        "  for i = 1, 10 do if i == 2 then goto continue end debugPrint(i) ::continue:: end\n"
        "  local a, b = 1, 2\n"
        "  a, b = b, a\n"
        "end\n";
    vscript::ConvertResult r = convert(lua, vscript::Base::Song);
    const std::string& s = r.source;
    CHECK(has(s, "list.push(5);"));
    CHECK(has(s, "var names:Dynamic = [\"a\", \"b\"];"));
    CHECK(has(s, "var cfg:Dynamic = {speed: 2, name: \"x\"};"));
    CHECK(has(s, "for (i in 1...list.length + 1)"));
    CHECK(has(s, "list[i - 1] = list[i - 1] * 2;"));
    CHECK(has(s, "for (i in 0...4)"));
    CHECK(has(s, "for (k in Reflect.fields(cfg))"));
    CHECK(has(s, "cfg.speed != null ? cfg.speed : 1"));
    CHECK(has(s, "x > 1 ? \"fast\" : \"slow\""));
    CHECK(has(s, "Std.int(10) + \" hits, \" + FlxMath.roundDecimal(99.5, 2) + \"%\""));
    CHECK(has(s, "\"abc\".toUpperCase() + \"hello\".substring(1, 3)"));
    CHECK(has(s, "up.indexOf(\"B\") >= 0"));
    CHECK(has(s, "Math.pow(x, 2)"));
    CHECK(has(s, "!Math.isNaN(Std.parseFloat(Std.string(x)))"));
    CHECK(has(s, "do"));
    CHECK(has(s, "while (!(x <= 0));"));
    CHECK(has(s, "continue;"));
    CHECK(has(s, "var tmp"));
    CHECK(braceBalance(s) == 0);
}

static void testNoteKindAndEvent() {
    vscript::ConvertOptions o;
    o.base = vscript::Base::NoteKind;
    o.className = "HurtNoteKind";
    o.id = "hurt";
    o.title = "Hurt Note";
    vscript::ConvertResult r = vscript::convertLua("function goodNoteHit(i, d, t, s) addHealth(-0.3) end\nfunction onBeatHit() end\nfunction onSongStart() debugPrint('x') end", o);
    CHECK(has(r.source, "class HurtNoteKind extends NoteKind"));
    CHECK(has(r.source, "super(\"hurt\", \"Hurt Note\");"));
    CHECK(has(r.source, "public override function onNoteHit(event:HitNoteScriptEvent):Void"));
    CHECK(lacks(r.source, "onSongStart(event"));
    CHECK(!r.issues.empty());

    vscript::ConvertOptions e;
    e.base = vscript::Base::SongEvent;
    e.className = "FlashEvent";
    e.id = "Flash";
    e.eventName = "Flash";
    vscript::ConvertResult ev = vscript::convertLua("function onEvent(n, a, b) if n == 'Flash' then cameraFlash('game', a, 0.3) end end", e);
    CHECK(has(ev.source, "class FlashEvent extends SongEvent"));
    CHECK(has(ev.source, "public override function handleEvent(data:SongEventData):Void"));
    CHECK(has(ev.source, "var a:Dynamic = Reflect.field(data.value, \"value1\");"));
    CHECK(has(ev.source, "getEventSchema"));
    CHECK(braceBalance(ev.source) == 0);
}

static void testErrorsAndApi() {
    char* raw = vscript_convert_lua("function onCreate( end", "{\"base\":\"stage\"}");
    bool ok = false;
    vscript::Json result = vscript::Json::parse(raw, ok);
    vscript_free(raw);
    CHECK(ok);
    CHECK(!result["ok"].asBool(true));
    CHECK(result["issues"].items().size() == 1);
    CHECK(result["issues"].items()[0]["line"].asNumber() == 1);

    raw = vscript_convert_lua("function onCreate() makeLuaSprite('a', 'b', 0, 0) end", "{\"base\":\"stage\",\"className\":\"AStage\",\"id\":\"a\"}");
    result = vscript::Json::parse(raw, ok);
    vscript_free(raw);
    CHECK(ok && result["ok"].asBool());
    CHECK(has(result["source"].text(), "class AStage extends Stage"));
    CHECK(result["callbacks"].items().size() == 1);

    raw = vscript_builtin_event("Add Camera Zoom", "AddCameraZoomEvent", "AddCameraZoom");
    result = vscript::Json::parse(raw, ok);
    vscript_free(raw);
    CHECK(result["known"].asBool());
    CHECK(has(result["source"].text(), "class AddCameraZoomEvent extends SongEvent"));
    CHECK(has(result["source"].text(), "import funkin.play.event.SongEvent;"));
    CHECK(braceBalance(result["source"].text()) == 0);
    raw = vscript_builtin_event("Unknown Thing", "X", "Y");
    result = vscript::Json::parse(raw, ok);
    vscript_free(raw);
    CHECK(!result["known"].asBool(true));

    raw = vscript_generate("{\"kind\":\"module\",\"className\":\"MyModule\",\"targetId\":\"my-module\",\"callbacks\":[\"onUpdate\",\"onNoteHit\"]}");
    result = vscript::Json::parse(raw, ok);
    vscript_free(raw);
    CHECK(result["ok"].asBool());
    CHECK(has(result["source"].text(), "super(\"my-module\", 1000);"));
    CHECK(has(result["source"].text(), "public override function onNoteHit(event:HitNoteScriptEvent):Void"));
    CHECK(has(result["source"].text(), "import funkin.modding.module.Module;"));
    raw = vscript_generate("{\"kind\":\"event\",\"className\":\"BoomEvent\",\"targetId\":\"Boom\",\"fields\":[{\"name\":\"power\",\"type\":\"float\"}]}");
    result = vscript::Json::parse(raw, ok);
    vscript_free(raw);
    CHECK(has(result["source"].text(), "var power:Float = value == null"));
    CHECK(has(result["source"].text(), "{name: \"power\", title: \"Power\", type: \"float\", defaultValue: 0.0}"));
    raw = vscript_generate("{\"kind\":\"weird\",\"className\":\"A\",\"targetId\":\"a\"}");
    result = vscript::Json::parse(raw, ok);
    vscript_free(raw);
    CHECK(!result["ok"].asBool(true));

    vscript::Json j = vscript::Json::parse("{\"a\":[1,2.5,\"\\u00e9\\n\"],\"b\":{\"c\":null,\"d\":true}}", ok);
    CHECK(ok);
    CHECK(j["a"].items()[2].text() == "\xC3\xA9\n");
    CHECK(j.dump() == "{\"a\":[1,2.5,\"\xC3\xA9\\n\"],\"b\":{\"c\":null,\"d\":true}}");
    CHECK(std::string(vscript_version()) == VSCRIPT_VERSION_STRING);
}

static void testFuzz() {
    const char* pieces[] = {"function", "end", "if", "then", "(", ")", "'x'", "local", "=", "1", "..", "{", "}", "[", "]", "for", "do", "a", ",", ":", "#", "--", "\n", "[[", "]]", "\"", "\\", "goto", "::"};
    unsigned seed = 1234;
    for (int round = 0; round < 3000; round++) {
        std::string text;
        int n = 1 + static_cast<int>(seed % 30);
        for (int i = 0; i < n; i++) {
            seed = seed * 1103515245u + 12345u;
            text += pieces[(seed >> 8) % (sizeof(pieces) / sizeof(pieces[0]))];
            text += ' ';
        }
        char* raw = vscript_convert_lua(text.c_str(), "{\"base\":\"song\"}");
        CHECK(raw != nullptr);
        vscript_free(raw);
    }
}

static std::string data(const char* kind, const std::string& input) {
    char* raw = vscript_convert_data(kind, input.c_str());
    std::string out = raw ? raw : "";
    vscript_free(raw);
    return out;
}

static void testDataConverters() {
    std::string chart = data("chart", R"({"charts":[{"difficulty":"Hard","text":"{\"song\":{\"song\":\"Test\",\"bpm\":150,\"speed\":2,\"player1\":\"bf\",\"player2\":\"dad\",\"stage\":\"spooky\",\"notes\":[{\"mustHitSection\":true,\"sectionNotes\":[[0,0,0],[0,0,0],[400,5,100,\"Hurt Note\"]]}],\"events\":[[100,[[\"Set GF Speed\",\"2\",\"\"],[\"Hey!\",\"bf\",\"\"]]]]}}"}],"options":{}})");
    CHECK(has(chart, "\"ok\":true"));
    CHECK(has(chart, "\"stage\":\"spookyMansion\""));
    CHECK(has(chart, "\"kind\":\"SetTargetBopSpeed\",\"value\":{\"target\":\"girlfriend\",\"rate\":2}"));
    CHECK(has(chart, "\"kind\":\"hurt\""));
    CHECK(has(chart, "\"duplicateNotes\":[{\"name\":\"hard\",\"count\":1}]"));
    CHECK(has(data("chart", R"({"charts":[{"difficulty":"hard","text":"nope"}]})"), "\"ok\":false"));
    CHECK(has(data("chart", "{}"), "Select at least one Psych Engine chart."));

    std::string character = data("character", R"({"id":"dude","slot":"opponent","atlases":{"dude":[["dude idle0000",410,400]]},"text":"{\"image\":\"characters/Dude\",\"scale\":1.5,\"position\":[20,300],\"animations\":[{\"anim\":\"idle\",\"name\":\"dude idle\",\"fps\":24,\"offsets\":[-5,3]}]}"})");
    CHECK(has(character, "\"ok\":true"));
    CHECK(has(character, "\"version\":\"1.0.2\""));
    CHECK(has(character, "\"name\":\"Dude\""));

    std::string stage = data("stage", R"({"id":"my-stage","atlases":{},"stage":{"directory":"mymod","defaultZoom":0.8,"objects":[{"type":"square","name":"fade","scale":[1280,720],"color":"black","filters":3},{"type":"sprite","name":"bg","image":"images/bg.png","filters":3,"scroll":[0.9,0.9]},{"type":"boyfriend"}]}})");
    CHECK(has(stage, "\"ok\":true"));
    CHECK(has(stage, "\"version\":\"1.0.2\""));
    CHECK(has(stage, "\"name\":\"My Stage\""));
    CHECK(has(stage, "\"assetPath\":\"#000000\""));
    CHECK(has(stage, "\"assetPath\":\"bg\""));
    CHECK(has(stage, "\"directory\":\"shared\""));
    CHECK(has(data("stage", "{}"), "The stage is missing."));
    CHECK(has(data("other", "{}"), "\"ok\":false"));
    CHECK(has(data("stage", "not json"), "\"ok\":false"));
}

static void testChangeCharacter() {
    bool known = false;
    std::string code = vscript::builtinEvent("ChangeCharacter", "ChangeCharacterEvent", "ChangeCharacter", known);
    CHECK(known);
    CHECK(has(code, "import funkin.data.character.CharacterDataParser;"));
    CHECK(has(code, "import funkin.play.character.CharacterType;"));
    CHECK(has(code, "import funkin.data.song.SongEventData;"));
    CHECK(has(code, "super(\"ChangeCharacter\", {processOldEvents: true});"));
    CHECK(has(code, "stage.addCharacter(next, slot == 1 ? CharacterType.DAD : (slot == 2 ? CharacterType.GF : CharacterType.BF));"));
    CHECK(has(code, "pool.push(characterAt(stage, slot, true));"));
    CHECK(has(code, "songEvent.eventKind != \"ChangeCharacter\""));
    CHECK(has(code, "return \"ChangeCharacter\";"));
    CHECK(braceBalance(code) == 0);
    CHECK(lacks(code, "trace("));

    std::string alt = vscript::builtinEvent("Alt Idle Animation", "AltIdleAnimationEvent", "AltIdleAnimation", known);
    CHECK(has(alt, "index == 1 ? stage.getBoyfriend()"));
    CHECK(has(alt, ": stage.getDad());"));

    auto r = convert(R"(function onBeatHit()
    triggerEvent('Change Character', 'dad', 'pico')
    triggerEvent('Hey!', 'gf', '')
    triggerEvent('Hey!', '', '')
    triggerEvent('Set GF Speed', '2', '')
    triggerEvent('Play Animation', 'shoot', '')
end)", vscript::Base::Song);
    CHECK(has(r.source, "new SongEventData(Conductor.instance.songPosition, \"ChangeCharacter\", {value1: \"dad\", value2: \"pico\"})"));
    CHECK(has(r.source, "{anim: \"cheer\", target: \"gf\", force: true}"));
    CHECK(has(r.source, "{anim: \"hey\", target: \"bf\", force: true}"));
    CHECK(has(r.source, "\"SetTargetBopSpeed\", {target: \"girlfriend\", rate: Std.parseInt(Std.string(\"2\"))}"));
    CHECK(has(r.source, "{anim: \"shoot\", target: \"dad\", force: true}"));
    CHECK(braceBalance(r.source) == 0);

    std::string chart = data("chart", R"({"charts":[{"difficulty":"normal","text":"{\"song\":{\"song\":\"T\",\"bpm\":120,\"notes\":[],\"events\":[[0,[[\"ChangeCharacter\",\"bf\",\"pico\"],[\"Change Character\",\"dad\",\"x\"],[\"Screen Shake\",\"1,0.1\",\"\"]]]]}}"}],"options":{"keepPsychEvents":false}})");
    CHECK(has(chart, "\"kind\":\"ChangeCharacter\",\"value\":{\"value1\":\"bf\",\"value2\":\"pico\"}"));
    CHECK(has(chart, "\"scriptedEvents\":[{\"name\":\"Change Character\",\"count\":2},{\"name\":\"Screen Shake\",\"count\":1}]"));
    CHECK(has(chart, "\"unsupportedEvents\":[]"));
}

static void testRuntimeSafety() {
    const std::string lua =
        "function onCreate()\n"
        "  if not lowQuality then makeAnimatedLuaSprite('sayori', 'clubroom/sayori', 100, 200) end\n"
        "  debugPrint('a\\1b\\27c\\0d')\n"
        "end\n"
        "function onBeatHit()\n"
        "  objectPlayAnimation('sayori', 'idle', true)\n"
        "  playAnim('monika', 'idle', true)\n"
        "  setProperty('dad.visible', getProperty('sayori.animation.curAnim.curFrame') > 2)\n"
        "end\n"
        "function onEvent(n, a, b)\n"
        "  doTweenZoom('camz', 'camGame', 1.2, 1, 'quadOut')\n"
        "end\n"
        "function onTweenCompleted(tag, vars)\n"
        "  if tag == 'camz' then setProperty('defaultCamZoom', 1.2) end\n"
        "end\n";
    vscript::ConvertResult r = convert(lua, vscript::Base::Stage, "ClubroomStage", "clubroom");
    CHECK(r.ok);
    const std::string& s = r.source;
    CHECK(lacks(s, "\\x"));
    CHECK(has(s, "String.fromCharCode(0)"));
    CHECK(has(s, "sayori?.animation?.play(\"idle\", true, false, 0);"));
    CHECK(has(s, "getNamedProp(\"monika\")?.playAnimation(\"idle\", true);"));
    CHECK(has(s, "getDad()?.visible = sayori?.animation?.curAnim?.curFrame > 2;"));
    CHECK(has(s, "function onTweenCompleted(?tag, ?vars)"));
    CHECK(has(s, "onTweenCompleted(\"camz\")"));
}

static vscript::HaxeLuaResult hxToLua(const std::string& source, const std::string& engine = "vslice", const std::string& file = "Script.hxc") {
    vscript::HaxeLuaOptions options;
    options.engine = engine;
    options.fileName = file;
    return vscript::convertHaxeToLua(source, options);
}

static void testHaxeToLua() {
    const std::string stage =
        "import funkin.play.stage.Stage;\n"
        "import funkin.graphics.FunkinSprite;\n"
        "import flixel.FlxG;\n"
        "import flixel.tweens.FlxTween;\n"
        "import flixel.tweens.FlxEase;\n"
        "import flixel.util.FlxTimer;\n"
        "class ClubStage extends Stage {\n"
        "  var sayori:FunkinSprite;\n"
        "  var bops:Int = 0;\n"
        "  var crowd:Array<FunkinSprite> = [];\n"
        "  function new() { super('club'); }\n"
        "  override function buildStage() {\n"
        "    super.buildStage();\n"
        "    sayori = FunkinSprite.createSparrow(100, 200, 'club/sayori');\n"
        "    sayori.animation.addByPrefix('idle', 'Sayori Idle', 24, false);\n"
        "    sayori.scrollFactor.set(0.9, 0.9);\n"
        "    add(sayori);\n"
        "    for (i in 0...3) {\n"
        "      var c = FunkinSprite.create(i * 100, 500, 'club/crowd');\n"
        "      crowd.push(c);\n"
        "      add(c);\n"
        "    }\n"
        "  }\n"
        "  override function onBeatHit(event:SongTimeScriptEvent) {\n"
        "    super.onBeatHit(event);\n"
        "    if (event.beat % 2 == 0) sayori.animation.play('idle', true);\n"
        "    getNamedProp('monika').playAnimation('idle', true);\n"
        "    for (c in crowd) c.y += 5;\n"
        "    bops++;\n"
        "    if (bops > 3) {\n"
        "      FlxTween.tween(FlxG.camera, {zoom: 1.2}, 1, {ease: FlxEase.quadOut, onComplete: function(t) { trace('done ' + bops); }});\n"
        "      new FlxTimer().start(0.5, function(tmr:FlxTimer) { getBoyfriend().playAnimation('hey', true); });\n"
        "    }\n"
        "  }\n"
        "  override function onUpdate(event:UpdateScriptEvent) {\n"
        "    PlayState.instance.health += event.elapsed * 0.01;\n"
        "    var label = 'x${bops}';\n"
        "    switch (bops) { case 1, 2: trace('low'); default: trace(label.toUpperCase()); }\n"
        "    FlxG.state.openSubState(new PauseSubState());\n"
        "  }\n"
        "}\n";
    vscript::HaxeLuaResult r = hxToLua(stage);
    CHECK(r.ok);
    CHECK(r.kind == "stage");
    CHECK(r.className == "ClubStage");
    CHECK(r.targetId == "club");
    const std::string& s = r.source;
    CHECK(has(s, "function onCreate()"));
    CHECK(has(s, "makeAnimatedLuaSprite('sayori', 'club/sayori', 100, 200)"));
    CHECK(has(s, "addAnimationByPrefix('sayori', 'idle', 'Sayori Idle', 24, false)"));
    CHECK(has(s, "setScrollFactor('sayori', 0.9, 0.9)"));
    CHECK(has(s, "addLuaSprite('sayori', false)"));
    CHECK(has(s, "for i = 0, 3 - 1 do"));
    CHECK(has(s, "local c = 'c_' .. hx_uid()\n    makeLuaSprite(c, 'club/crowd', i * 100, 500)"));
    CHECK(has(s, "table.insert(crowd, c)"));
    CHECK(has(s, "function onBeatHit()"));
    CHECK(has(s, "if curBeat % 2 == 0 then"));
    CHECK(has(s, "playAnim('sayori', 'idle', true)"));
    CHECK(has(s, "playAnim('monika', 'idle', true)"));
    CHECK(has(s, "setProperty(c .. '.y', getProperty(c .. '.y') + 5)"));
    CHECK(has(s, "bops = bops + 1"));
    CHECK(has(s, "startTween("));
    CHECK(has(s, "'camGame', {zoom = 1.2}, 1, {ease = 'quadOut', onComplete = 'hxTweenDone_ClubStage'})"));
    CHECK(has(s, "debugPrint('done ' .. tostring(bops))"));
    CHECK(has(s, "runTimer("));
    CHECK(has(s, "playAnim('boyfriend', 'hey', true)"));
    CHECK(has(s, "function onTimerCompleted(tag, loops, loopsLeft)"));
    CHECK(has(s, "function hxTweenDone_ClubStage(tag)"));
    CHECK(has(s, "function onUpdate(elapsed)"));
    CHECK(has(s, "setProperty('health', getProperty('health') + elapsed * 0.01)"));
    CHECK(has(s, "local label = 'x' .. tostring(bops)"));
    CHECK(has(s, "if hx_sw"));
    CHECK(has(s, "debugPrint(string.upper(label))"));
    CHECK(has(s, "runHaxeCode([[FlxG.state.openSubState(new PauseSubState())]])"));
    CHECK(r.fallbacks == 1);
    CHECK(lacks(s, "super"));
    CHECK(lacks(s, "event."));

    const std::string event =
        "class ZoomEvent extends SongEvent {\n"
        "  public function new() { super('ZoomCam'); }\n"
        "  public override function handleEvent(data:SongEventData) {\n"
        "    if (PlayState.instance == null) return;\n"
        "    var zoom:Float = data.getFloat('zoom');\n"
        "    PlayState.instance.currentCameraZoom = zoom;\n"
        "  }\n"
        "  public override function getTitle():String { return 'Zoom Cam'; }\n"
        "  public override function getEventSchema() { return new SongEventSchema([{name: 'zoom', type: 'float'}, {name: 'dur', type: 'float'}]); }\n"
        "}\n";
    r = hxToLua(event);
    CHECK(r.ok);
    CHECK(r.kind == "event");
    CHECK(r.title == "Zoom Cam");
    CHECK(r.schema.size() == 2);
    CHECK(has(r.source, "function onEvent(name, value1, value2, strumTime)"));
    CHECK(has(r.source, "if name ~= 'ZoomCam' then return end"));
    CHECK(has(r.source, "if false then"));
    CHECK(has(r.source, "local zoom = tonumber(value1)"));
    CHECK(has(r.source, "setProperty('defaultCamZoom', zoom)"));

    const std::string note =
        "class HurtNote extends NoteKind {\n"
        "  function new() { super('hurt'); }\n"
        "  override function onNoteHit(event:HitNoteScriptEvent) {\n"
        "    if (event.note.noteData.getMustHitNote()) PlayState.instance.health -= 0.3;\n"
        "    trace(event.note.noteData.getDirection());\n"
        "  }\n"
        "}\n";
    r = hxToLua(note);
    CHECK(r.ok);
    CHECK(has(r.source, "function goodNoteHit(id, direction, noteType, isSustainNote)\n  if noteType ~= 'hurt' then return end\n  if true then"));
    CHECK(has(r.source, "function opponentNoteHit(id, direction, noteType, isSustainNote)"));
    CHECK(has(r.source, "debugPrint(tostring(direction))"));

    const std::string codename =
        "var bg:FlxSprite;\n"
        "function create() {\n"
        "  bg = new FlxSprite(-100, -50).loadGraphic(Paths.image('stages/bg'));\n"
        "  bg.scale.set(6, 6);\n"
        "  bg.cameras = [camHUD];\n"
        "  insert(members.indexOf(dad), bg);\n"
        "}\n"
        "function beatHit(curBeat:Int) {\n"
        "  if (curBeat % 4 == 0) FlxG.camera.zoom += 0.015;\n"
        "  if (curBeat == 8) FlxG.camera.flash(FlxColor.WHITE, 1);\n"
        "}\n"
        "function onPlayerHit(event) {\n"
        "  if (event.noteType == 'Hurt Note') event.cancel();\n"
        "}\n"
        "function onEvent(e) {\n"
        "  if (e.event.name == 'Flash') trace(e.event.params[0]);\n"
        "  var list = [for (i in 0...4) if (i != 2) i * 2];\n"
        "}\n";
    r = hxToLua(codename, "codename", "songs/test/scripts/zoom.hx");
    CHECK(r.ok);
    CHECK(r.kind == "script");
    CHECK(has(r.source, "makeLuaSprite('bg', '', -100, -50)\n  loadGraphic('bg', 'stages/bg')"));
    CHECK(has(r.source, "scaleObject('bg', 6, 6, false)"));
    CHECK(has(r.source, "setObjectCamera('bg', 'hud')"));
    CHECK(has(r.source, "addLuaSprite('bg', false)"));
    CHECK(has(r.source, "setProperty('camGame.zoom', getProperty('camGame.zoom') + 0.015)"));
    CHECK(has(r.source, "cameraFlash('game', 'FFFFFF', 1, false)"));
    CHECK(has(r.source, "local hx_stop = false"));
    CHECK(has(r.source, "if hx_stop then return Function_Stop end"));
    CHECK(has(r.source, "if name == 'Flash' then"));
    CHECK(has(r.source, "debugPrint(tostring(value1))"));
    CHECK(has(r.source, "hx_out[#hx_out + 1] = i * 2"));
    CHECK(r.fallbacks == 0);

    r = hxToLua("class Broken extends Module { function new( { }");
    CHECK(!r.ok);
    CHECK(!r.issues.empty());

    const char* pieces[] = {"class", "X", "extends", "Module", "{", "}", "function", "(", ")", "var", "=", "1", "'a$b'", "if", "else", "for", "in", "...",
        "[", "]", ";", ".", "?.", "switch", "case", ":", "new", "FlxSprite", "->", "return", "\"s\"", "?", "!", "+", "=>", "cast", "try", "catch", "@:meta", "#if"};
    unsigned seed = 99;
    for (int round = 0; round < 3000; round++) {
        std::string text;
        int n = 1 + static_cast<int>(seed % 40);
        for (int i = 0; i < n; i++) {
            seed = seed * 1103515245u + 12345u;
            text += pieces[(seed >> 8) % (sizeof(pieces) / sizeof(pieces[0]))];
            text += ' ';
        }
        char* raw = vscript_convert_hx(text.c_str(), round % 2 ? "{\"engine\":\"codename\"}" : "{}");
        CHECK(raw != nullptr);
        vscript_free(raw);
    }
    for (size_t cut = 0; cut < stage.size(); cut += 7) {
        char* raw = vscript_convert_hx(stage.substr(0, cut).c_str(), "{}");
        CHECK(raw != nullptr);
        vscript_free(raw);
    }
}

static void testDoctor() {
    vscript::DoctorOptions lua;
    const std::string broken =
        "// stage\n"
        "function oncreate()\n"
        "  makeluaSprite('bg', 'back', 0, 0)\n"
        "  luaSpriteAddAnimationByPrefix('bg', 'idle', 'bg idle', 24, true)\n"
        "  setPropertyLuaSprite('bg', 'alpha', 0.5)\n"
        "  var count = 0\n"
        "  if getPropertyFromClass('ClientPrefs', 'downScroll') && count != 1 then\n"
        "    count += 1\n"
        "  end\n"
        "end\n"
        "function onBeatHit()\n"
        "  if curBeat % 2 == 0\n"
        "    objectPlayAnimation('bg', 'idle', true, 0)\n"
        "    characterPlayAnim('bf', 'hey', true)\n"
        "  else if curBeat % 3 == 0 then\n"
        "    trace('three')\n"
        "  end\n"
        "end\n"
        "function onUpdate(elapsed: Float)\n"
        "  if !inCutscene then setProperty('health', 1) end\n"
        "end\n";
    vscript::DoctorResult r = vscript::fixLuaScript(broken, lua);
    const std::string& s = r.source;
    CHECK(r.changed);
    CHECK(has(s, "-- stage"));
    CHECK(has(s, "function onCreate()"));
    CHECK(has(s, "makeLuaSprite('bg', 'back', 0, 0)"));
    CHECK(has(s, "addAnimationByPrefix('bg', 'idle', 'bg idle', 24, true)"));
    CHECK(has(s, "setProperty('bg.alpha', 0.5)"));
    CHECK(has(s, "local count = 0"));
    CHECK(has(s, "getPropertyFromClass('backend.ClientPrefs', 'data.downScroll') and count ~= 1 then"));
    CHECK(has(s, "count = count + 1"));
    CHECK(has(s, "if curBeat % 2 == 0 then"));
    CHECK(has(s, "playAnim('bg', 'idle', true, false, 0)"));
    CHECK(has(s, "playAnim('boyfriend', 'hey', true)"));
    CHECK(has(s, "elseif curBeat % 3 == 0 then"));
    CHECK(has(s, "debugPrint('three')"));
    CHECK(has(s, "function onUpdate(elapsed)"));
    CHECK(has(s, "if not inCutscene then"));
    CHECK(r.problems.empty());
    bool parsed = true;
    try {
        vscript::parseLua(s);
    } catch (...) {
        parsed = false;
    }
    CHECK(parsed);

    const std::string good =
        "local tags = {}\n"
        "local function spawn(name) makeLuaSprite(name, 'x', 0, 0) table.insert(tags, name) end\n"
        "function onCreatePost() spawn('a') helper(1) local f = function(v) return v end f(2) end\n"
        "function helper(n) return n + 1 end\n"
        "function onEvent(name, v1, v2) if name ~= 'x' then return end end\n";
    r = vscript::fixLuaScript(good, lua);
    CHECK(!r.changed);
    CHECK(r.fixes.empty());
    CHECK(r.problems.empty());

    vscript::DoctorOptions hx;
    hx.id = "spooky";
    const std::string stage =
        "import states.PlayState;\n"
        "import funkin.play.Conductor;\n"
        "import backend.ClientPrefs;\n"
        "class SpookyStage extends Stage\n"
        "{\n"
        "  var light:FunkinSprite;\n"
        "  function onCreate(event)\n"
        "  {\n"
        "    light = FunkinSprite.create(0, 0, Paths.image('halloween_bg'));\n"
        "    FlxTween.tween(light, {alpha: 1}, 0.5, {ease: FlxEase.quadOut});\n"
        "  }\n"
        "  function onUpdate(elapsed:Float)\n"
        "  {\n"
        "    if (ClientPrefs.data.flashing) light.alpha -= elapsed;\n"
        "    trace(\"time \\x41 \" + Conductor.songPosition);\n"
        "  }\n"
        "  override function onBeathit(event:SongTimeScriptEvent)\n"
        "  {\n"
        "    super.onBeatHit(event);\n"
        "    game.boyfriend.playAnim('scared', true);\n"
        "  }\n"
        "}\n";
    r = vscript::fixHaxeScript(stage, hx);
    const std::string& h = r.source;
    CHECK(has(h, "import funkin.play.PlayState;"));
    CHECK(has(h, "import funkin.Conductor;"));
    CHECK(lacks(h, "ClientPrefs"));
    CHECK(has(h, "import flixel.tweens.FlxTween;"));
    CHECK(has(h, "import flixel.tweens.FlxEase;"));
    CHECK(has(h, "import funkin.graphics.FunkinSprite;"));
    CHECK(has(h, "import funkin.play.stage.Stage;"));
    CHECK(has(h, "public function new()"));
    CHECK(has(h, "super(\"spooky\");"));
    CHECK(has(h, "super.onCreate(event);"));
    CHECK(has(h, "FunkinSprite.create(0, 0, 'halloween_bg')"));
    CHECK(has(h, "function onUpdate(event:UpdateScriptEvent)"));
    CHECK(has(h, "light.alpha -= event.elapsed;"));
    CHECK(has(h, "Preferences.flashingLights"));
    CHECK(has(h, "\"time A \" + Conductor.instance.songPosition"));
    CHECK(has(h, "function onBeatHit(event:SongTimeScriptEvent)"));
    CHECK(has(h, "PlayState.instance.currentStage.getBoyfriend().playAnimation('scared', true);"));
    CHECK(lacks(h, "\\x"));
    bool hxParsed = true;
    try {
        vscript::parseHaxe(h);
    } catch (...) {
        hxParsed = false;
    }
    CHECK(hxParsed);
    for (const vscript::Issue& i : r.fixes) {
        if (i.message.find("FlxTween was used") != std::string::npos) CHECK(i.line == 10);
        if (i.message.find("\"elapsed\" is") != std::string::npos) CHECK(i.line == 12);
    }

    const std::string fine =
        "import funkin.play.song.Song;\n"
        "import funkin.modding.events.ScriptEvent;\n"
        "class DadSong extends Song {\n"
        "  function new() { super('dad'); }\n"
        "  function onSongRetry(event:ScriptEvent) { super.onSongRetry(event); }\n"
        "  public override function onSongEnd(event:CountdownScriptEvent):Void { super.onSongEnd(event); var p:FlxPoint = null; }\n"
        "}\n";
    r = vscript::fixHaxeScript(fine, hx);
    CHECK(!r.changed);
    CHECK(r.problems.empty());

    const char* pieces[] = {"function", "end", "if", "then", "(", ")", "'x'", "local", "=", "+=", "!=", "&&", "!", "//", "/*", "*/", "else if",
        "makeluaSprite", "objectPlayAnimation", "getPropertyLuaSprite", "class", "extends", "Stage", "{", "}", "import", ";", "\"\\x4\"", "onCreate", ","};
    unsigned seed = 7;
    for (int round = 0; round < 3000; round++) {
        std::string text;
        int n = 1 + static_cast<int>(seed % 40);
        for (int i = 0; i < n; i++) {
            seed = seed * 1103515245u + 12345u;
            text += pieces[(seed >> 8) % (sizeof(pieces) / sizeof(pieces[0]))];
            text += (seed >> 20) % 4 == 0 ? '\n' : ' ';
        }
        char* a = vscript_fix_lua(text.c_str(), "{}");
        char* b = vscript_fix_hx(text.c_str(), round % 2 ? "{\"engine\":\"codename\"}" : "{}");
        CHECK(a != nullptr && b != nullptr);
        vscript_free(a);
        vscript_free(b);
    }
}

static void testEventOverrides() {
    vscript::ConvertOptions e;
    e.base = vscript::Base::SongEvent;
    e.className = "ShaderGlitchEvent";
    e.id = "ShaderGlitch";
    e.eventName = "ShaderGlitch";
    vscript::ConvertResult r = vscript::convertLua(
        "local t = 0\n"
        "function onEvent(n, a, b) t = 1 end\n"
        "function onUpdate(elapsed) t = t + elapsed end\n"
        "function onDestroy() t = 0 end\n", e);
    const std::string& s = r.source;
    CHECK(has(s, "public override function onUpdate(event:UpdateScriptEvent):Void"));
    CHECK(has(s, "public override function onDestroy(event:ScriptEvent):Void"));
    CHECK(has(s, "if (!eventInSong() || PlayState.instance == null) return;"));
    CHECK(has(s, "function eventInSong():Bool"));
    CHECK(has(s, "public override function handleEvent(data:SongEventData):Void"));
    CHECK(lacks(s, "function onUpdate(elapsed"));
    CHECK(lacks(s, "function onDestroy()"));
    CHECK(braceBalance(s) == 0);

    vscript::ConvertResult c = convert("function onCreate() setProperty('skipCountdown', true) end\nfunction onCountdownTick(t) debugPrint(t) end", vscript::Base::Song);
    CHECK(has(c.source, "var skipCountdown:Bool = false;"));
    CHECK(has(c.source, "Countdown.skipCountdown();"));
    CHECK(has(c.source, "import funkin.play.Countdown;"));
    CHECK(lacks(c.source, "PlayState.instance.skipCountdown"));
    CHECK(braceBalance(c.source) == 0);

    vscript::ConvertResult sh = convert("function onCreate() makeLuaSprite('a', 'bg', 0, 0) setSpriteShader('a', 'chromToggle') end", vscript::Base::Song);
    CHECK(has(sh.source, "Assets.exists(Paths.frag("));

    vscript::ConvertResult x = convert("function onCreate() local x = 2 local alpha = x end", vscript::Base::Song);
    CHECK(has(x.source, "var x = 2;"));
    vscript::ConvertOptions m;
    m.base = vscript::Base::Module;
    m.className = "Glitch";
    m.id = "glitch";
    vscript::ConvertResult mod = vscript::convertLua("function onDestroy() debugPrint('a') end\nfunction onUpdatePost(e) end\nfunction onCustom() onDestroy() end", m);
    int destroys = 0;
    for (size_t at = mod.source.find("function onDestroy"); at != std::string::npos; at = mod.source.find("function onDestroy", at + 1)) destroys++;
    CHECK(destroys == 1);
    CHECK(braceBalance(mod.source) == 0);
    vscript::ConvertResult gx = convert("x = 2\nfunction onCreate() x = 3 end", vscript::Base::Stage);
    CHECK(has(gx.source, "x_"));
}

static void testShader() {
    vscript::ShaderOptions o;
    vscript::ShaderResult toy = vscript::convertShader(
        "#version 330\n"
        "uniform sampler2D iChannel0;\n"
        "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
        "  vec2 uv = fragCoord / iResolution.xy;\n"
        "  fragColor = texture(iChannel0, uv + sin(iTime));\n"
        "}\n", o);
    CHECK(toy.changed);
    CHECK(has(toy.source, "#pragma header"));
    CHECK(lacks(toy.source, "#version"));
    CHECK(has(toy.source, "uniform float iTime;"));
    CHECK(has(toy.source, "#define iResolution vec3(openfl_TextureSize, 0.0)"));
    CHECK(has(toy.source, "flixel_texture2D(bitmap, uv + sin(iTime))"));
    CHECK(has(toy.source, "mainImage(gl_FragColor, openfl_TextureCoordv * openfl_TextureSize);"));
    CHECK(toy.problems.empty());

    vscript::ShaderResult es = vscript::convertShader(
        "#pragma header\n"
        "in vec2 uv2;\n"
        "out vec4 outColor;\n"
        "uniform float amount;\n"
        "void main() { outColor = texture(bitmap, openfl_TextureCoordv) * amount; }\n", o);
    CHECK(has(es.source, "varying vec2 uv2;"));
    CHECK(has(es.source, "gl_FragColor = flixel_texture2D(bitmap, openfl_TextureCoordv) * amount;"));
    CHECK(lacks(es.source, "outColor"));
    CHECK(es.uniforms.size() == 1 && es.uniforms[0] == "amount");

    vscript::ShaderResult same = vscript::convertShader("#pragma header\nvoid main() { gl_FragColor = flixel_texture2D(bitmap, openfl_TextureCoordv); }\n", o);
    CHECK(!same.changed);
    CHECK(same.fixes.empty());

    vscript::ShaderOptions v;
    v.vertex = true;
    vscript::ShaderResult vert = vscript::convertShader("#version 300 es\nin vec4 pos;\nout vec2 uv;\nvoid main() { gl_Position = pos; }\n", v);
    CHECK(has(vert.source, "attribute vec4 pos;"));
    CHECK(has(vert.source, "varying vec2 uv;"));

    vscript::ShaderResult broken = vscript::convertShader("uniform float a;\n", o);
    CHECK(!broken.problems.empty());

    char* raw = vscript_convert_shader("#version 330\nvoid main() { gl_FragColor = vec4(1.0); }", "{\"name\":\"x\"}");
    bool ok = false;
    vscript::Json j = vscript::Json::parse(raw, ok);
    vscript_free(raw);
    CHECK(ok);
    CHECK(j["changed"].asBool(false));
    CHECK(lacks(j["source"].asString(), "#version"));
}

static void testImage() {
    vscript::Image image;
    image.width = 37;
    image.height = 23;
    image.rgba.resize(size_t(image.width) * image.height * 4);
    for (int y = 0; y < image.height; y++) for (int x = 0; x < image.width; x++) {
        uint8_t* p = &image.rgba[(size_t(y) * image.width + x) * 4];
        p[0] = uint8_t(x * 7);
        p[1] = uint8_t(y * 11);
        p[2] = uint8_t((x * y) & 0xFF);
        p[3] = uint8_t(x < 5 ? 0 : 255 - y);
    }
    std::vector<uint8_t> png = vscript::encodePng(image, 6);
    CHECK(png.size() > 33 && png[25] == 6);
    vscript::Image back;
    std::string error;
    CHECK(vscript::decodePng(png.data(), png.size(), back, error));
    CHECK(back.width == 37 && back.height == 23 && back.rgba == image.rgba);

    vscript::Image opaque = image;
    for (size_t i = 3; i < opaque.rgba.size(); i += 4) opaque.rgba[i] = 255;
    std::vector<uint8_t> rgb = vscript::encodePng(opaque, 6);
    CHECK(rgb[25] == 2);
    CHECK(vscript::decodePng(rgb.data(), rgb.size(), back, error) && back.rgba == opaque.rgba);
    CHECK(!vscript::decodePng(rgb.data(), 20, back, error));

    vscript::Image half = vscript::resizeImage(opaque, 18, 11, true);
    CHECK(half.width == 18 && half.height == 11 && half.rgba[3] == 255);
    vscript::Image big = vscript::resizeImage(opaque, 74, 46, false);
    CHECK(big.rgba[0] == opaque.rgba[0] && big.rgba[(size_t(45) * 74 + 73) * 4] == opaque.rgba[(size_t(22) * 37 + 36) * 4]);

    vscript::Image red;
    red.width = 1;
    red.height = 1;
    red.rgba = {255, 0, 0, 255};
    vscript::bakeColors(red, 0xFFC24B99u, 0xFFFFFFFFu, 0xFF3C1F56u);
    CHECK(red.rgba[0] == 0xC2 && red.rgba[1] == 0x4B && red.rgba[2] == 0x99);

    int w = 0, h = 0;
    unsigned char* pixels = vscript_png_decode(png.data(), png.size(), &w, &h);
    CHECK(pixels && w == 37 && h == 23);
    vscript_free_bytes(pixels);
}

static void testReverseFiles() {
    const char* names[] = {"metadata:", "chart:"};
    const char* texts[] = {
        "{\"songName\":\"Tiny\",\"timeChanges\":[{\"t\":0,\"bpm\":120,\"n\":4}],\"playData\":{\"difficulties\":[\"normal\",\"erect\"],\"characters\":{\"player\":\"bf\",\"opponent\":\"dad\"},\"stage\":\"mainStage\",\"noteStyle\":\"funkin\"}}",
        "{\"scrollSpeed\":{\"default\":2},\"events\":[{\"t\":0,\"e\":\"FocusCamera\",\"v\":{\"char\":0}},{\"t\":10,\"e\":\"Weird\",\"v\":[1,2]}],\"notes\":{\"normal\":[{\"t\":2500,\"d\":1,\"l\":0,\"k\":\"alt\"},{\"t\":0.5,\"d\":9,\"l\":100}]}}"
    };
    char** files = nullptr;
    size_t count = 0;
    char* header = vscript_convert_files("vslice-psych-charts", names, texts, 2, "{\"songId\":\"Tiny\"}", &files, &count);
    bool ok = false;
    vscript::Json h = vscript::Json::parse(header, ok);
    CHECK(ok && h["ok"].asBool(false));
    CHECK(count == 2);
    CHECK(count == 2 && has(files[0], "\"song\":\"Tiny\"") && has(files[0], "[2500,1,0,\"Alt Animation\"]") && has(files[0], "[0.5,7,100]"));
    CHECK(count == 2 && has(files[0], "\"mustHitSection\":true") && has(files[0], "[\"Weird\",\"[1,2]\",\"\"]"));
    vscript_free(header);
    vscript_free_files(files, count);

    const char* probe[] = {"{\"playData\":{},\"x\":[1]}"};
    const char* probeNames[] = {""};
    header = vscript_convert_files("json-keys", probeNames, probe, 1, "{}", &files, &count);
    CHECK(has(header, "[\"playData\",\"object\"]") && has(header, "[\"x\",\"array\"]"));
    vscript_free(header);
    vscript_free_files(files, count);
    header = vscript_convert_files("nope", probeNames, probe, 1, "{}", &files, &count);
    CHECK(has(header, "\"ok\":false"));
    vscript_free(header);
    vscript_free_files(files, count);
}

int main() {
    testParser();
    testStage();
    testSongEvents();
    testModuleScope();
    testLuaSemantics();
    testNoteKindAndEvent();
    testErrorsAndApi();
    testFuzz();
    testDataConverters();
    testChangeCharacter();
    testRuntimeSafety();
    testHaxeToLua();
    testDoctor();
    testEventOverrides();
    testShader();
    testImage();
    testReverseFiles();
    if (failures == 0) std::printf("v-script: all tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
