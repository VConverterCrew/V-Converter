package com.vconvertercrew.vconverter

import com.vconvertercrew.vconverter.convert.NmvConverters
import com.vconvertercrew.vconverter.convert.NmvTab
import com.vconvertercrew.vconverter.convert.PickedText
import com.vconvertercrew.vconverter.convert.chart.ChartSource
import com.vconvertercrew.vconverter.convert.chart.ConversionOptions
import com.vconvertercrew.vconverter.convert.chart.psych.PsychChartParser
import com.vconvertercrew.vconverter.convert.chart.PsychToVSliceChart
import com.vconvertercrew.vconverter.convert.mod.ModInfo
import com.vconvertercrew.vconverter.convert.mod.ModScanner
import com.vconvertercrew.vconverter.engines.luaslice.LuaSlice
import com.vconvertercrew.vconverter.engines.luaslice.LuaSliceModScanner
import com.vconvertercrew.vconverter.engines.luaslice.LuaSliceToPsych
import com.vconvertercrew.vconverter.engines.luaslice.ToLuaSliceMod
import com.vconvertercrew.vconverter.json.JSONObject
import com.vconvertercrew.vconverter.tools.playtest.MemoryModFiles
import com.vconvertercrew.vconverter.tools.playtest.MemoryModWriter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LuaSliceTest {
    private val chart = """{"song":{"song":"Black Knife","notes":[{"sectionNotes":[[0,0,0,"Bonus"],[500,5,0,"Hurt Note"],[750,1,0]],"mustHitSection":true,"lengthInSteps":16}],
        "events":[[100,[["My Event","a","b"],["Change Character","dad","pico"],["Change Character","1","gf-car"],["Add Camera Zoom","0.1","0.2"]]]],
        "bpm":120,"needsVoices":false,"player1":"bf","player2":"dad","gfVersion":"gf","stage":"stage","speed":1.5}}"""

    private fun psychMod(): MemoryModFiles = MemoryModFiles(mapOf(
        "mod/data/Black Knife/black knife.json" to chart,
        "mod/data/Black Knife/knight.lua" to "function onCreate() addLuaScript('scripts/helper.lua') end",
        "mod/scripts/global.lua" to "function onEvent(name, v1, v2) if name == 'My Event' then debugPrint(v1) end end",
        "mod/scripts/helper.lua" to "function onCreatePost() end",
        "mod/stages/stage.lua" to "function onCreate() makeLuaSprite('bg', 'stageback', -600, -200) addLuaSprite('bg', false) end",
        "mod/characters/pico.lua" to "function onUpdate() end",
        "mod/custom_events/My Event.lua" to "function onEvent(n) end",
        "mod/custom_notetypes/Bonus.lua" to "function goodNoteHit(id, d, noteType) if noteType == 'Bonus' then addHealth(0.04) end end",
        "mod/scripts/extra.hx" to "function onCreate() { trace('hi'); }"
    ).mapValues { it.value.encodeToByteArray() })

    @Test
    fun pathsMatchLuaSliceLoader() {
        assertEquals("data/songs/black knife/knight.luap", LuaSlice.toLuaSlicePath("data/Black Knife/knight.lua"))
        assertEquals("scripts/global.luap", LuaSlice.toLuaSlicePath("scripts/global.lua"))
        assertEquals("stages/stage.luap", LuaSlice.toLuaSlicePath("stages/stage.lua"))
        assertEquals("custom_events/My Event.luap", LuaSlice.toLuaSlicePath("custom_events/My Event.lua"))
        assertEquals("scripts/global.lua", LuaSlice.toPsychPath("global.luapg"))
        assertEquals("data/black-knife/knight.lua", LuaSlice.toPsychPath("data/songs/black-knife/knight.luap") { it })
        assertEquals("custom_notetypes/Bonus.lua", LuaSlice.toPsychPath("custom_notetypes/Bonus.luap"))
        assertEquals("addLuaScript('scripts/a.luap')", LuaSlice.toLuaSliceText("addLuaScript('scripts/a.lua')"))
        assertEquals("addLuaScript(\"scripts/a.lua\")", LuaSlice.toPsychText("addLuaScript(\"scripts/a.luapg\")"))
        assertEquals("require('x.lua')", LuaSlice.toLuaSliceText("require('x.lua')"))
        assertEquals("custom_events/E.lua", LuaSlice.modRelative("C:/mods/thing/custom_events/E.lua"))
        assertEquals("scripts/loose.lua", LuaSlice.modRelative("loose.lua"))
    }

    @Test
    fun chartKeepsPsychNamesForLuaSlice() {
        val source = ChartSource("normal", PsychChartParser.parse(chart))
        for (native in listOf(true, false)) {
            val options = ConversionOptions(luaSlice = true)
            val result = if (native) PsychToVSliceChart.convert(listOf(source), options) else PsychToVSliceChart.convertKotlin(listOf(source), options)
            val json = JSONObject(result.files("black knife").first { it.first.contains("-chart") }.second)
            val events = json.getJSONArray("events")
            val kinds = (0 until events.length()).map { events.getJSONObject(it).getString("e") }
            assertTrue(native.toString() + kinds, "My Event" in kinds)
            assertTrue("ChangeCharacter" in kinds)
            assertFalse("MyEvent" in kinds)
            val changes = (0 until events.length()).map { events.getJSONObject(it) }.filter { it.getString("e") == "ChangeCharacter" }
            assertEquals(listOf(1, 2), changes.map { it.getJSONObject("v").getInt("char") })
            assertEquals(listOf("pico", "gf-car"), changes.map { it.getJSONObject("v").getString("id") })
            val notes = json.getJSONObject("notes").getJSONArray("normal")
            val noteKinds = (0 until notes.length()).mapNotNull { notes.getJSONObject(it).optString("k").takeIf { k -> k.isNotEmpty() } }
            assertEquals(listOf("Bonus", "hurt"), noteKinds)
        }
        val luaOptions = ConversionOptions(luaSlice = true, idEvents = setOf("Camera Follow Pos"))
        assertEquals(PsychToVSliceChart.convertKotlin(listOf(source), luaOptions).files("black knife"), PsychToVSliceChart.convert(listOf(source), luaOptions).files("black knife"))
        val plain = PsychToVSliceChart.convert(listOf(source), ConversionOptions())
        val plainJson = plain.files("black knife").first { it.first.contains("-chart") }.second
        assertTrue(plainJson.contains("\"MyEvent\""))
        assertTrue(Regex("\"value2\"\\s*:\\s*\"pico\"").containsMatchIn(plainJson))
    }

    @Test
    fun psychModKeepsLuaAsLuap() {
        val files = psychMod()
        val writer = MemoryModWriter()
        val result = ToLuaSliceMod.fromPsych(files, ModScanner.scan(files), writer, ModInfo("Knife", "", ""))
        val out = writer.files.keys
        listOf("data/songs/black knife/knight.luap", "scripts/global.luap", "scripts/helper.luap", "stages/stage.luap", "characters/pico.luap",
            "custom_events/My Event.luap", "custom_notetypes/Bonus.luap", "_polymod_meta.json").forEach { assertTrue("$it in $out", it in out) }
        assertTrue(out.none { it.endsWith(".lua") })
        assertTrue(out.any { it.endsWith(".hxc") })
        assertTrue(writer.files["data/songs/black knife/knight.luap"]!!.decodeToString().contains("addLuaScript('scripts/helper.luap')"))
        val stage = JSONObject(writer.files["data/stages/stage.json"]!!.decodeToString())
        assertTrue(stage.getJSONArray("props").length() == 0 || (0 until stage.getJSONArray("props").length()).none { stage.getJSONArray("props").getJSONObject(it).optString("name") == "bg" })
        assertTrue(result.report.sections.any { it.title == "LuaSlice" })
        assertEquals("7", result.report.stats.first { it.label == "Lua scripts kept" }.value.toString())
    }

    @Test
    fun luaSliceModBackToPsych() {
        val files = psychMod()
        val writer = MemoryModWriter()
        ToLuaSliceMod.fromPsych(files, ModScanner.scan(files), writer, ModInfo("Knife", "", ""))
        val luaSliceFiles = MemoryModFiles(writer.files.mapKeys { "out/" + it.key } + mapOf("out/root.luapg" to "function onCreate() end".encodeToByteArray(), "out/scripts/native.luag" to "x = 1".encodeToByteArray()))
        assertTrue(LuaSliceModScanner.looksLikeLuaSlice(luaSliceFiles))
        val scan = LuaSliceModScanner.scan(luaSliceFiles)
        assertTrue(scan.base.ignored.none { it.endsWith(".luap") || it.endsWith(".luapg") })
        val back = MemoryModWriter()
        val result = LuaSliceToPsych.convert(luaSliceFiles, scan, back, ModInfo("Knife", "", ""))
        val out = back.files.keys
        listOf("scripts/global.lua", "scripts/root.lua", "stages/stage.lua", "characters/pico.lua", "custom_events/My Event.lua", "custom_notetypes/Bonus.lua").forEach { assertTrue("$it in $out", it in out) }
        assertTrue(out.any { it.startsWith("data/") && it.endsWith("/knight.lua") })
        assertTrue(back.files.getValue("scripts/helper.lua").decodeToString().isNotEmpty())
        assertTrue(result.report.warnings.any { "native.luag" in it })
        val psychChart = back.files.entries.first { it.key.startsWith("data/") && it.key.endsWith(".json") && "events" !in it.key && back.files[it.key]!!.decodeToString().contains("\"song\"") }.value.decodeToString()
        val events = back.files.entries.firstOrNull { it.key.endsWith("events.json") }?.value?.decodeToString() ?: psychChart
        assertTrue(events, events.contains("Change Character") || psychChart.contains("Change Character"))
        assertTrue((events + psychChart).contains("My Event"))
    }

    @Test
    fun fileConvertersRoute() {
        val scripts = NmvConverters.create("psych", "luaslice", NmvTab.SCRIPTS)
        assertEquals("scripts-luaslice.zip", scripts.downloadName())
        val lua = PickedText("mod/scripts/a.lua", "a.lua", "mod/scripts/a.lua", "function onCreate() end", "lua", "")
        val out = scripts.build(listOf(lua))
        assertEquals(listOf("scripts/a.luap"), out.files.map { it.relativePath })
        val back = NmvConverters.create("luaslice", "psych", NmvTab.SCRIPTS)
        val luap = PickedText("m/data/songs/song-a/x.luap", "x.luap", "m/data/songs/song-a/x.luap", "function onCreate() end", "luap", "")
        assertEquals(listOf("data/song-a/x.lua"), back.build(listOf(luap)).files.map { it.relativePath })
        assertEquals("charts-luaslice.zip", NmvConverters.create("psych", "luaslice", NmvTab.CHARTS).downloadName())
    }
}
