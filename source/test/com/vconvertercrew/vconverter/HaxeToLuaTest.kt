package com.vconvertercrew.vconverter

import com.vconvertercrew.vconverter.engines.codename.CodenamePsychScriptContext
import com.vconvertercrew.vconverter.engines.codename.CodenameScope
import com.vconvertercrew.vconverter.engines.codename.CodenameScriptTarget
import com.vconvertercrew.vconverter.engines.codename.CodenameToPsychLua
import com.vconvertercrew.vconverter.convert.reverse.HxcToPsych
import com.vconvertercrew.vconverter.convert.script.HaxeToLua
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HaxeToLuaTest {
    private val stage = """
        import funkin.play.stage.Stage;
        import funkin.graphics.FunkinSprite;
        class ClubStage extends Stage
        {
          var sayori:FunkinSprite;
          function new() { super('club'); }
          override function buildStage()
          {
            super.buildStage();
            sayori = FunkinSprite.createSparrow(100, 200, 'club/sayori');
            sayori.animation.addByPrefix('idle', 'Sayori Idle', 24, false);
            add(sayori);
          }
          override function onBeatHit(event:SongTimeScriptEvent)
          {
            super.onBeatHit(event);
            if (event.beat % 2 == 0) sayori.animation.play('idle', true);
          }
        }
    """.trimIndent()

    @Test
    fun vsliceClassBecomesPsychLua() {
        if (System.getProperty("vconverter.script.library") == null) {
            val plain = HxcToPsych.toLua(HxcToPsych.convert("ClubStage.hxc", stage), stage, "ClubStage.hxc")
            assertEquals("stages/club.hx", plain.files.single().path)
            return
        }
        assertTrue(HaxeToLua.available)
        val result = HxcToPsych.toLua(HxcToPsych.convert("ClubStage.hxc", stage), stage, "ClubStage.hxc")
        val file = result.files.single()
        assertEquals("stages/club.lua", file.path)
        assertTrue(file.content.contains("makeAnimatedLuaSprite('sayori', 'club/sayori', 100, 200)"))
        assertTrue(file.content.contains("function onBeatHit()"))
        assertTrue(file.content.contains("playAnim('sayori', 'idle', true)"))
        assertTrue(result.notes.first().startsWith("Converted to Psych Lua"))
    }

    @Test
    fun codenameScriptBecomesPsychLua() {
        val source = """
            function beatHit(curBeat:Int) {
                if (curBeat % 4 == 0) FlxG.camera.zoom += 0.015;
            }
        """.trimIndent()
        val target = CodenameScriptTarget(CodenameScope.SONG, "test")
        val lua = CodenameToPsychLua.convert("data/test/zoom.hx", "zoom.hx", source, target, CodenamePsychScriptContext())
        if (System.getProperty("vconverter.script.library") == null) {
            assertNull(lua)
            return
        }
        assertEquals("data/test/zoom.lua", lua!!.first)
        assertTrue(lua.second.source.contains("function onBeatHit()"))
        assertTrue(lua.second.source.contains("setProperty('camGame.zoom', getProperty('camGame.zoom') + 0.015)"))
        assertNull(CodenameToPsychLua.convert("stages/extensions/x.hx", "x.hx", source, target, CodenamePsychScriptContext()))
    }
}
