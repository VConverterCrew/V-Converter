package com.vconvertercrew.vconverter.notestyle

import com.vconvertercrew.vconverter.core.RgbaImage
import com.vconvertercrew.vconverter.editor.AtlasFrame
import com.vconvertercrew.vconverter.editor.SpriteAtlas

enum class NoteDir(val key: String, val color: String, val upper: String) {
    LEFT("left", "purple", "LEFT"),
    DOWN("down", "blue", "DOWN"),
    UP("up", "green", "UP"),
    RIGHT("right", "red", "RIGHT");

    val pascal: String get() = key.replaceFirstChar { it.uppercaseChar() }
}

class Sheet(val path: String, val image: RgbaImage, val atlas: SpriteAtlas)

class Anim(
    val sheet: Sheet,
    val frames: List<AtlasFrame>,
    val fps: Double = 24.0,
    val looped: Boolean = false,
    val variance: Double = 0.0
) {
    val first: AtlasFrame get() = frames.first()

    companion object {
        fun of(sheet: Sheet, prefix: String, fps: Double = 24.0, looped: Boolean = false, indices: List<Int> = emptyList(), variance: Double = 0.0): Anim? {
            if (prefix.isEmpty()) return null
            val all = sheet.atlas.framesFor(prefix, indices)
            val exact = all.filter { frame -> frame.name.removePrefix(prefix).all { it.isDigit() } }
            val frames = if (indices.isEmpty() && exact.isNotEmpty()) exact else all
            return if (frames.isEmpty()) null else Anim(sheet, frames, fps, looped, variance)
        }
    }
}

class StrumAnims(val static: Anim, val press: Anim?, val confirm: Anim?)

class HoldAnims(val piece: Anim, val end: Anim)

class Rgb(val r: Int, val g: Int, val b: Int)

object NoteColors {
    val psych: List<Rgb> = listOf(
        Rgb(0xFFC24B99.toInt(), 0xFFFFFFFF.toInt(), 0xFF3C1F56.toInt()),
        Rgb(0xFF00FFFF.toInt(), 0xFFFFFFFF.toInt(), 0xFF1542B7.toInt()),
        Rgb(0xFF12FA05.toInt(), 0xFFFFFFFF.toInt(), 0xFF0A4447.toInt()),
        Rgb(0xFFF9393F.toInt(), 0xFFFFFFFF.toInt(), 0xFF651038.toInt())
    )
}

class NoteStyle(
    val id: String,
    val name: String,
    val author: String,
    val notes: Map<NoteDir, Anim>,
    val strums: Map<NoteDir, StrumAnims>,
    val holds: Map<NoteDir, HoldAnims>,
    val splashes: Map<NoteDir, List<Anim>>,
    val noteScale: Double = 0.7,
    val strumScale: Double = 0.7,
    val holdScale: Double = 0.7,
    val splashScale: Double = 1.0,
    val splashAlpha: Double = 0.6,
    val pixel: Boolean = false,
    val rgb: List<Rgb>? = null,
    val splashRgb: List<Rgb>? = null,
    val info: List<String> = emptyList()
) {
    val complete: Boolean get() = NoteDir.entries.all { it in notes }
    val hasStrums: Boolean get() = NoteDir.entries.all { it in strums }
    val hasHolds: Boolean get() = NoteDir.entries.all { it in holds }
    val hasSplashes: Boolean get() = NoteDir.entries.any { splashes[it].orEmpty().isNotEmpty() }
}

class NoteStyleInput(val path: String, val text: String?, val image: RgbaImage?)

class NoteStyleOutput(val path: String, val text: String?, val bytes: ByteArray?)

class NoteStyleResult(val files: List<NoteStyleOutput>, val sections: List<Pair<String, List<String>>>, val warnings: List<String>)
