package com.vconvertercrew.vconverter.convert.mod

import com.vconvertercrew.vconverter.convert.script.VScriptNative
import com.vconvertercrew.vconverter.platform.ByteArrayInputStream
import com.vconvertercrew.vconverter.platform.InputStream
import com.vconvertercrew.vconverter.platform.PlatformLock

object SpriteOptimizer {
    const val LABEL = "Optimize Sprites"
    const val DESCRIPTION = "Recompresses every PNG of the converted mod without losing quality (palette, grayscale and stronger compression). Takes longer; images that can't get smaller stay the same."
    const val LEVEL = 6

    var enabled: Boolean = false

    val available: Boolean get() = VScriptNative.available

    fun wrap(base: ModWriter): ModWriter = if (enabled && available && base !is OptimizingModWriter) OptimizingModWriter(base) else base

    fun optimize(png: ByteArray): ByteArray = VScriptNative.pngOptimize(png, LEVEL)?.takeIf { it.size < png.size } ?: png

    fun isSprite(path: String): Boolean = path.endsWith(".png", ignoreCase = true)
}

class OptimizingModWriter(private val base: ModWriter) : ModWriter {
    var saved: Long = 0
        private set
    var optimized: Int = 0
        private set
    private val lock = PlatformLock()

    override fun writeText(path: String, text: String) = base.writeText(path, text)

    override fun copy(path: String, source: () -> InputStream) {
        if (!SpriteOptimizer.isSprite(path)) return base.copy(path, source)
        base.copy(path) { ByteArrayInputStream(shrink(source().use { it.readBytes() })) }
    }

    override fun copyFrom(path: String, files: ModFiles, source: String) {
        if (!SpriteOptimizer.isSprite(path)) return base.copyFrom(path, files, source)
        copy(path) { files.open(source) }
    }

    private fun shrink(bytes: ByteArray): ByteArray {
        val out = SpriteOptimizer.optimize(bytes)
        if (out !== bytes) {
            lock.withLock {
                saved += bytes.size - out.size
                optimized++
            }
        }
        return out
    }
}
