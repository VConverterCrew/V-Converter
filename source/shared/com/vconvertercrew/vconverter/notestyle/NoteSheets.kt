package com.vconvertercrew.vconverter.notestyle

import com.vconvertercrew.vconverter.core.RgbaImage
import com.vconvertercrew.vconverter.editor.AtlasFrame
import com.vconvertercrew.vconverter.editor.SpriteAtlas
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sqrt

object NoteImages {
    fun region(sheet: Sheet, frame: AtlasFrame): RgbaImage {
        val src = sheet.image
        val w = if (frame.rotated) frame.height else frame.width
        val h = if (frame.rotated) frame.width else frame.height
        val out = RgbaImage.blank(max(1, w), max(1, h))
        for (dy in 0 until h) for (dx in 0 until w) {
            val ax = if (frame.rotated) frame.x + (frame.width - 1 - dy) else frame.x + dx
            val ay = if (frame.rotated) frame.y + dx else frame.y + dy
            if (ax < 0 || ay < 0 || ax >= src.width || ay >= src.height) continue
            src.pixels.copyInto(out.pixels, (dy * w + dx) * 4, (ay * src.width + ax) * 4, (ay * src.width + ax) * 4 + 4)
        }
        return out
    }

    fun full(sheet: Sheet, frame: AtlasFrame): RgbaImage {
        val part = region(sheet, frame)
        val fw = max(frame.frameWidth, part.width - frame.frameX)
        val fh = max(frame.frameHeight, part.height - frame.frameY)
        val out = RgbaImage.blank(max(1, fw), max(1, fh))
        paste(out, part, -frame.frameX, -frame.frameY)
        return out
    }

    fun paste(target: RgbaImage, source: RgbaImage, x: Int, y: Int) {
        for (row in 0 until source.height) {
            val ty = y + row
            if (ty < 0 || ty >= target.height) continue
            for (col in 0 until source.width) {
                val tx = x + col
                if (tx < 0 || tx >= target.width) continue
                source.pixels.copyInto(target.pixels, (ty * target.width + tx) * 4, (row * source.width + col) * 4, (row * source.width + col) * 4 + 4)
            }
        }
    }

    fun bake(image: RgbaImage, rgb: Rgb): RgbaImage {
        val r = channels(rgb.r)
        val g = channels(rgb.g)
        val b = channels(rgb.b)
        val out = image.pixels.copyOf()
        for (i in 0 until image.width * image.height) {
            val o = i * 4
            if (out[o + 3].toInt() and 0xFF == 0) continue
            val cr = (out[o].toInt() and 0xFF) / 255.0
            val cg = (out[o + 1].toInt() and 0xFF) / 255.0
            val cb = (out[o + 2].toInt() and 0xFF) / 255.0
            for (c in 0 until 3) {
                val v = min(1.0, cr * r[c] + cg * g[c] + cb * b[c])
                out[o + c] = (v * 255.0).roundToInt().toByte()
            }
        }
        return RgbaImage(image.width, image.height, out)
    }

    private fun channels(argb: Int): DoubleArray = doubleArrayOf(((argb ushr 16) and 0xFF) / 255.0, ((argb ushr 8) and 0xFF) / 255.0, (argb and 0xFF) / 255.0)

    fun resize(image: RgbaImage, width: Int, height: Int, smooth: Boolean): RgbaImage {
        val w = max(1, width)
        val h = max(1, height)
        if (w == image.width && h == image.height) return image
        val out = RgbaImage.blank(w, h)
        val sx = image.width.toDouble() / w
        val sy = image.height.toDouble() / h
        if (!smooth) {
            for (y in 0 until h) {
                val py = min(image.height - 1, floor((y + 0.5) * sy).toInt())
                for (x in 0 until w) {
                    val px = min(image.width - 1, floor((x + 0.5) * sx).toInt())
                    image.pixels.copyInto(out.pixels, (y * w + x) * 4, (py * image.width + px) * 4, (py * image.width + px) * 4 + 4)
                }
            }
            return out
        }
        val acc = DoubleArray(4)
        for (y in 0 until h) {
            val y0 = y * sy
            val y1 = min(image.height.toDouble(), (y + 1) * sy)
            for (x in 0 until w) {
                val x0 = x * sx
                val x1 = min(image.width.toDouble(), (x + 1) * sx)
                acc.fill(0.0)
                var area = 0.0
                if (sx >= 1.0 && sy >= 1.0) {
                    var py = floor(y0).toInt()
                    while (py < ceil(y1).toInt()) {
                        val wy = min(y1, py + 1.0) - max(y0, py.toDouble())
                        var px = floor(x0).toInt()
                        while (px < ceil(x1).toInt()) {
                            val wx = min(x1, px + 1.0) - max(x0, px.toDouble())
                            val weight = wx * wy
                            if (weight > 0) {
                                accumulate(image, px, py, weight, acc)
                                area += weight
                            }
                            px++
                        }
                        py++
                    }
                } else {
                    val fx = (x + 0.5) * sx - 0.5
                    val fy = (y + 0.5) * sy - 0.5
                    val ix = floor(fx).toInt()
                    val iy = floor(fy).toInt()
                    val tx = fx - ix
                    val ty = fy - iy
                    for ((ox, oy, weight) in listOf(Triple(0, 0, (1 - tx) * (1 - ty)), Triple(1, 0, tx * (1 - ty)), Triple(0, 1, (1 - tx) * ty), Triple(1, 1, tx * ty))) {
                        if (weight <= 0) continue
                        accumulate(image, (ix + ox).coerceIn(0, image.width - 1), (iy + oy).coerceIn(0, image.height - 1), weight, acc)
                        area += weight
                    }
                }
                if (area <= 0) continue
                val o = (y * w + x) * 4
                val a = acc[3] / area
                if (a <= 0.0) continue
                for (c in 0 until 3) out.pixels[o + c] = (acc[c] / acc[3] * 255.0).roundToInt().coerceIn(0, 255).toByte()
                out.pixels[o + 3] = (a * 255.0).roundToInt().coerceIn(0, 255).toByte()
            }
        }
        return out
    }

    private fun accumulate(image: RgbaImage, x: Int, y: Int, weight: Double, acc: DoubleArray) {
        val o = (y * image.width + x) * 4
        val a = (image.pixels[o + 3].toInt() and 0xFF) / 255.0 * weight
        acc[0] += (image.pixels[o].toInt() and 0xFF) / 255.0 * a
        acc[1] += (image.pixels[o + 1].toInt() and 0xFF) / 255.0 * a
        acc[2] += (image.pixels[o + 2].toInt() and 0xFF) / 255.0 * a
        acc[3] += a
    }

    fun average(image: RgbaImage): DoubleArray {
        val sum = DoubleArray(3)
        var weight = 0.0
        for (i in 0 until image.width * image.height) {
            val a = (image.pixels[i * 4 + 3].toInt() and 0xFF) / 255.0
            if (a <= 0.1) continue
            for (c in 0 until 3) sum[c] += (image.pixels[i * 4 + c].toInt() and 0xFF) * a
            weight += a
        }
        return if (weight == 0.0) sum else DoubleArray(3) { sum[it] / weight }
    }

    fun center(sheet: Sheet, frame: AtlasFrame): Pair<Double, Double> {
        val part = region(sheet, frame)
        var sx = 0.0
        var sy = 0.0
        var total = 0.0
        for (y in 0 until part.height) for (x in 0 until part.width) {
            val a = (part.pixels[(y * part.width + x) * 4 + 3].toInt() and 0xFF) / 255.0
            if (a <= 0.05) continue
            sx += (x + 0.5) * a
            sy += (y + 0.5) * a
            total += a
        }
        if (total == 0.0) return frame.frameWidth / 2.0 to frame.frameHeight / 2.0
        return sx / total - frame.frameX to sy / total - frame.frameY
    }

    fun isTemplate(notes: Map<NoteDir, Anim>): Boolean {
        val firsts = NoteDir.entries.mapNotNull { notes[it] }
        if (firsts.size < 4) return false
        val keys = firsts.map { "${it.first.x},${it.first.y},${it.first.width},${it.first.height}" }.toSet()
        if (keys.size == 1 && firsts.map { it.sheet }.toSet().size == 1) return true
        val colors = firsts.map { average(region(it.sheet, it.first)) }
        var widest = 0.0
        for (i in colors.indices) for (j in i + 1 until colors.size) {
            val d = sqrt((0 until 3).sumOf { (colors[i][it] - colors[j][it]).let { v -> v * v } })
            widest = max(widest, d)
        }
        return widest < 24.0
    }
}

class SheetBuilder(private val factor: Double = 1.0, private val smooth: Boolean = true) {
    private class Region(val image: RgbaImage) {
        var x = 0
        var y = 0
    }

    private class Entry(val name: String, val region: Region, val frameX: Int, val frameY: Int, val frameWidth: Int, val frameHeight: Int)

    private val ids = HashMap<Sheet, Int>()
    private val regions = LinkedHashMap<String, Region>()
    private val entries = ArrayList<Entry>()

    val isEmpty: Boolean get() = entries.isEmpty()

    fun add(name: String, anim: Anim?, rgb: Rgb? = null, scale: Double = factor) {
        anim ?: return
        anim.frames.forEachIndexed { i, frame -> addFrame(name + i.toString().padStart(4, '0'), anim.sheet, frame, rgb, scale) }
    }

    fun addFrame(name: String, sheet: Sheet, frame: AtlasFrame, rgb: Rgb? = null, scale: Double = factor) {
        val sheetId = ids.getOrPut(sheet) { ids.size }
        val key = "$sheetId|${frame.x},${frame.y},${frame.width},${frame.height},${frame.rotated}|${rgb?.let { "${it.r},${it.g},${it.b}" }}|$scale"
        val region = regions.getOrPut(key) {
            var image = NoteImages.region(sheet, frame)
            if (rgb != null) image = NoteImages.bake(image, rgb)
            if (scale != 1.0) image = NoteImages.resize(image, (image.width * scale).roundToInt(), (image.height * scale).roundToInt(), smooth)
            Region(image)
        }
        val fx = (frame.frameX * scale).roundToInt()
        val fy = (frame.frameY * scale).roundToInt()
        val fw = max((frame.frameWidth * scale).roundToInt(), region.image.width - fx)
        val fh = max((frame.frameHeight * scale).roundToInt(), region.image.height - fy)
        entries += Entry(name, region, fx, fy, fw, fh)
    }

    fun build(imageName: String): Pair<RgbaImage, String> {
        val pad = 2
        val list = regions.values.sortedByDescending { it.image.height }
        val widest = list.maxOfOrNull { it.image.width } ?: 1
        val area = list.sumOf { (it.image.width + pad).toLong() * (it.image.height + pad) }
        val limit = max(widest + pad, min(4096, ceil(sqrt(area * 1.15)).toInt()))
        var x = 0
        var y = 0
        var row = 0
        var width = 1
        list.forEach { r ->
            if (x > 0 && x + r.image.width > limit) {
                x = 0
                y += row + pad
                row = 0
            }
            r.x = x
            r.y = y
            x += r.image.width + pad
            row = max(row, r.image.height)
            width = max(width, r.x + r.image.width)
        }
        val height = max(1, y + row)
        val image = RgbaImage.blank(width, height)
        list.forEach { NoteImages.paste(image, it.image, it.x, it.y) }
        val xml = StringBuilder()
        xml.append("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<TextureAtlas imagePath=\"").append(escape(imageName)).append("\">\n")
        entries.forEach { e ->
            xml.append("\t<SubTexture name=\"").append(escape(e.name)).append("\" x=\"").append(e.region.x).append("\" y=\"").append(e.region.y)
                .append("\" width=\"").append(e.region.image.width).append("\" height=\"").append(e.region.image.height).append('"')
            if (e.frameX != 0 || e.frameY != 0 || e.frameWidth != e.region.image.width || e.frameHeight != e.region.image.height) {
                xml.append(" frameX=\"").append(e.frameX).append("\" frameY=\"").append(e.frameY)
                    .append("\" frameWidth=\"").append(e.frameWidth).append("\" frameHeight=\"").append(e.frameHeight).append('"')
            }
            xml.append("/>\n")
        }
        xml.append("</TextureAtlas>\n")
        return image to xml.toString()
    }

    private fun escape(text: String) = text.replace("&", "&amp;").replace("\"", "&quot;").replace("<", "&lt;").replace(">", "&gt;")
}

object HoldStrip {
    fun build(holds: Map<NoteDir, HoldAnims>, pixel: Boolean, rgb: List<Rgb>?): RgbaImage {
        fun prepare(anim: Anim, dir: NoteDir): RgbaImage {
            val image = NoteImages.full(anim.sheet, anim.first)
            return rgb?.let { NoteImages.bake(image, it[dir.ordinal]) } ?: image
        }
        val pieces = NoteDir.entries.associateWith { prepare(holds.getValue(it).piece, it) }
        val ends = NoteDir.entries.associateWith { prepare(holds.getValue(it).end, it) }
        val column = max(pieces.values.maxOf { it.width }, ends.values.maxOf { it.width })
        val endHeight = ends.values.maxOf { it.height }
        val height = if (pixel) endHeight else ceil(endHeight / 0.9).toInt()
        val out = RgbaImage.blank(column * 8, max(1, height))
        NoteDir.entries.forEach { dir ->
            val piece = NoteImages.resize(pieces.getValue(dir), pieces.getValue(dir).width, height, !pixel)
            NoteImages.paste(out, piece, dir.ordinal * 2 * column + (column - piece.width) / 2, 0)
            val end = ends.getValue(dir)
            NoteImages.paste(out, end, (dir.ordinal * 2 + 1) * column + (column - end.width) / 2, 0)
        }
        return out
    }

    fun read(path: String, image: RgbaImage, pixel: Boolean): Map<NoteDir, HoldAnims> {
        val column = image.width / 8
        if (column <= 0 || image.height <= 0) return emptyMap()
        val endHeight = if (pixel) image.height else max(1, (image.height * 0.9).roundToInt())
        val frames = NoteDir.entries.flatMap { dir ->
            listOf(
                AtlasFrame("${dir.color} hold piece0000", dir.ordinal * 2 * column, 0, column, image.height, 0, 0, column, image.height, false),
                AtlasFrame("${dir.color} hold end0000", (dir.ordinal * 2 + 1) * column, 0, column, endHeight, 0, 0, column, endHeight, false)
            )
        }
        val sheet = Sheet(path, image, SpriteAtlas(frames))
        return NoteDir.entries.associateWith { dir ->
            HoldAnims(Anim.of(sheet, "${dir.color} hold piece")!!, Anim.of(sheet, "${dir.color} hold end")!!)
        }
    }
}
