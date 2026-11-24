package com.vconvertercrew.vconverter

import com.vconvertercrew.vconverter.reverse.HxcToPsych
import com.vconvertercrew.vconverter.script.ScriptConverter
import com.vconvertercrew.vconverter.script.ScriptDoctor
import com.vconvertercrew.vconverter.script.ScriptSource
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ScriptDoctorTest {
    private val native get() = System.getProperty("vconverter.script.library") != null

    @Test
    fun brokenLuaIsFixedBeforeConverting() {
        val lua = """
            function oncreate()
              makeluaSprite('bg', 'stageback', -600, -200)
              addLuaSprite('bg', false)
            end
            function onBeatHit()
              if curBeat % 2 == 0
                objectPlayAnimation('bg', 'idle', true)
              end
            end
        """.trimIndent()
        val check = ScriptDoctor.lua(lua, "stages/spooky.lua")
        if (!native) {
            assertNull(check)
            return
        }
        assertTrue(check!!.changed)
        assertTrue(check.source.contains("function onCreate()"))
        assertTrue(check.source.contains("if curBeat % 2 == 0 then"))
        val bundle = ScriptConverter.convert("Spooky", listOf(ScriptSource("stages/spooky.lua", lua)), emptyMap())
        val code = bundle.files.single().content
        assertTrue(code.contains("bg = FunkinSprite.create(-600, -200, \"stageback\");"))
        assertTrue(bundle.report.sections.any { section -> section.lines.any { it.startsWith("Fixed line 1:") } })
    }

    @Test
    fun brokenHxcIsFixedBeforeConvertingToPsych() {
        val hxc = """
            class FlashModule extends Module
            {
              function onUpdate(elapsed:Float)
              {
                trace("t \x41 " + elapsed);
              }
            }
        """.trimIndent()
        val check = ScriptDoctor.haxe(hxc, "FlashModule.hxc")
        if (!native) {
            assertNull(check)
            return
        }
        assertTrue(check!!.source.contains("function onUpdate(event:UpdateScriptEvent)"))
        assertTrue(check.source.contains("event.elapsed"))
        assertTrue(check.source.contains("super(\"FlashModule\");"))
        assertFalse(check.source.contains("\\x"))
        val result = HxcToPsych.convertFixed("FlashModule.hxc", hxc)
        assertTrue(result.notes.any { it.startsWith("Fixed") })
        assertTrue(result.files.any { it.content.contains("onUpdate") })
    }
}
