package com.vconvertercrew.vconverter

import com.vconvertercrew.vconverter.convert.mod.ModConversionResult
import com.vconvertercrew.vconverter.convert.mod.ModRun
import com.vconvertercrew.vconverter.convert.mod.SpriteOptimizer
import com.vconvertercrew.vconverter.convert.script.VScriptNative
import com.vconvertercrew.vconverter.core.PngWriter
import com.vconvertercrew.vconverter.core.ReportBuilder
import com.vconvertercrew.vconverter.core.RgbaImage
import com.vconvertercrew.vconverter.platform.Platform
import com.vconvertercrew.vconverter.tools.playtest.MemoryModWriter
import java.io.ByteArrayInputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SpriteOptimizerTest {
    private fun sprite(): ByteArray {
        val width = 128
        val height = 96
        val pixels = ByteArray(width * height * 4)
        val colors = arrayOf(intArrayOf(0, 0, 0, 0), intArrayOf(255, 40, 40, 255), intArrayOf(30, 90, 200, 255), intArrayOf(250, 230, 20, 160))
        for (y in 0 until height) for (x in 0 until width) {
            val c = colors[((x / 8) + (y / 8)) % colors.size]
            for (k in 0 until 4) pixels[(y * width + x) * 4 + k] = c[k].toByte()
        }
        return PngWriter.encodeKotlin(RgbaImage(width, height, pixels))
    }

    @Test
    fun bundledAssetsMoved() {
        listOf("ui/menu/menuBG.png", "ui/NOTE_assets.xml", "ui/icons/icon-face.png", "ui/cursor/cursor.png", "ui/app/icon.png",
            "gameplay/menus/title.json", "gameplay/menus/mainMenu.json", "gameplay/menus/backButton.json", "gameplay/introText.txt", "gameplay/scripts/PsychApiLibrary.hxc"
        ).forEach { assertNotNull(it, Platform.resourceBytes(it)) }
    }

    @Test
    fun optimizesWithoutChangingPixels() {
        if (!VScriptNative.available) return
        val png = sprite()
        val out = SpriteOptimizer.optimize(png)
        assertTrue(out.size < png.size)
        val before = VScriptNative.pngDecode(png)!!
        val after = VScriptNative.pngDecode(out)!!
        assertEquals(before.first, after.first)
        assertEquals(before.second, after.second)
        assertTrue(before.third.contentEquals(after.third))
        assertTrue(SpriteOptimizer.optimize(out).size <= out.size)
        val junk = byteArrayOf(1, 2, 3)
        assertTrue(SpriteOptimizer.optimize(junk) === junk)
    }

    @Test
    fun modRunOptimizesOnlyWhenEnabled() {
        if (!VScriptNative.available) return
        val png = sprite()
        fun run(enabled: Boolean): Pair<MemoryModWriter, ModConversionResult> {
            SpriteOptimizer.enabled = enabled
            try {
                val memory = MemoryModWriter()
                val result = ModRun({}).execute(memory) { writer, _ ->
                    writer.copy("images/characters/dude.png") { ByteArrayInputStream(png) }
                    writer.writeText("images/characters/dude.xml", "<TextureAtlas/>")
                    ModConversionResult(2, ReportBuilder().build())
                }
                return memory to result
            } finally {
                SpriteOptimizer.enabled = false
            }
        }
        val (off, offResult) = run(false)
        assertTrue(off.files["images/characters/dude.png"]!!.contentEquals(png))
        assertTrue(offResult.report.stats.none { it.label == "Sprites optimized" })
        val (on, onResult) = run(true)
        assertTrue(on.files["images/characters/dude.png"]!!.size < png.size)
        assertEquals("<TextureAtlas/>", on.files["images/characters/dude.xml"]!!.decodeToString())
        assertEquals("1", onResult.report.stats.first { it.label == "Sprites optimized" }.value.toString())
    }
}
