package com.vconvertercrew.vconverter

import com.vconvertercrew.vconverter.core.PngWriter
import com.vconvertercrew.vconverter.core.RgbaImage
import com.vconvertercrew.vconverter.engines.fpsplus.FpsIcon
import com.vconvertercrew.vconverter.convert.notestyle.NoteImages
import com.vconvertercrew.vconverter.convert.notestyle.Rgb
import com.vconvertercrew.vconverter.convert.script.VScriptNative
import org.junit.Assert.assertTrue
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test

class NativeImageTest {
    private fun sample(): RgbaImage {
        val image = RgbaImage.blank(61, 29)
        for (y in 0 until 29) for (x in 0 until 61) {
            val o = (y * 61 + x) * 4
            image.pixels[o] = (x * 4).toByte()
            image.pixels[o + 1] = (y * 8).toByte()
            image.pixels[o + 2] = (x xor y).toByte()
            image.pixels[o + 3] = (if (x < 3) 0 else 255 - y).toByte()
        }
        return image
    }

    @Test
    fun pngRoundTripsThroughBothCodecs() {
        val image = sample()
        val png = PngWriter.encode(image)
        assertTrue(image.pixels.contentEquals(FpsIcon.decodeKotlin(png)!!.pixels))
        assertTrue(image.pixels.contentEquals(FpsIcon.decode(png)!!.pixels))
        val kotlinPng = PngWriter.encodeKotlin(image)
        assertTrue(image.pixels.contentEquals(FpsIcon.decode(kotlinPng)!!.pixels))
    }

    @Test
    fun nativeImageOpsMatchKotlin() {
        if (!VScriptNative.available) return
        val image = sample()
        val baked = NoteImages.bake(image, Rgb(0xFFC24B99.toInt(), 0xFFFFFFFF.toInt(), 0xFF3C1F56.toInt()))
        assertNotNull(VScriptNative.imageBake(image.pixels, 61, 29, 1, 2, 3))
        assertEquals(61, baked.width)
        val small = NoteImages.resize(image, 30, 14, true)
        assertEquals(30 * 14 * 4, small.pixels.size)
        val big = NoteImages.resize(image, 122, 58, false)
        assertEquals(image.pixels[(28 * 61 + 60) * 4 + 1], big.pixels[(57 * 122 + 121) * 4 + 1])
    }
}
