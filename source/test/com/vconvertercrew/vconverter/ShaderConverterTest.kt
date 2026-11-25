package com.vconvertercrew.vconverter

import com.vconvertercrew.vconverter.convert.character.BaseCharacterScripts
import com.vconvertercrew.vconverter.convert.script.ShaderConverter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ShaderConverterTest {
    private val native get() = System.getProperty("vconverter.script.library") != null

    @Test
    fun shadertoyBecomesRuntimeShader() {
        val toy = """
            #version 330
            void mainImage(out vec4 fragColor, in vec2 fragCoord) {
              vec2 uv = fragCoord / iResolution.xy;
              fragColor = texture(iChannel0, uv);
            }
        """.trimIndent()
        val result = ShaderConverter.convert(toy, "toy", "psych", "vslice")
        if (!native) {
            assertEquals(toy, result.source)
            assertFalse(result.changed)
            return
        }
        assertTrue(result.changed)
        assertTrue(result.source.contains("#pragma header"))
        assertTrue(result.source.contains("flixel_texture2D(bitmap, uv)"))
        assertFalse(result.source.contains("#version"))
        assertTrue(result.lines().isNotEmpty())
    }

    @Test
    fun shaderPaths() {
        assertTrue(ShaderConverter.isShader("shaders/glitch.frag"))
        assertTrue(ShaderConverter.isVertex("shaders/wave.vsh"))
        assertFalse(ShaderConverter.isShader("shaders/readme.txt"))
        assertEquals("shaders/wave.vert", ShaderConverter.targetPath("x/wave.vsh", "vslice"))
        assertEquals("data/shaders/glitch.frag", ShaderConverter.targetPath("shaders/glitch.glsl", "fpsplus"))
    }

    @Test
    fun baseCharacterScriptFollowsRenderType() {
        val (path, text) = BaseCharacterScripts.replacement("bf", """{"renderType":"sparrow","assetPath":"characters/BOYFRIEND"}""")!!
        assertEquals("scripts/characters/bf.hxc", path)
        assertTrue(text.contains("class BoyfriendCharacter extends SparrowCharacter"))
        assertTrue(text.contains("super(\"bf\");"))
        val atlas = BaseCharacterScripts.replacement("pico-blazin", """{"renderType":"multianimateatlas"}""")!!.second
        assertTrue(atlas.contains("extends MultiAnimateAtlasCharacter"))
        assertNull(BaseCharacterScripts.replacement("my-oc", "{}"))
    }
}
