package com.vconvertercrew.vconverter.convert.notestyle

import com.vconvertercrew.vconverter.convert.ConvertedOutput
import com.vconvertercrew.vconverter.convert.FileConverter
import com.vconvertercrew.vconverter.convert.PickedText
import com.vconvertercrew.vconverter.core.ReportBuilder
import com.vconvertercrew.vconverter.engines.fpsplus.FpsIcon
import com.vconvertercrew.vconverter.io.ImportedFile
import com.vconvertercrew.vconverter.io.OutputFile

class NoteStyleConverter(private val from: String, private val to: String) : FileConverter() {
    override val noun: String = "note style"

    override val engine: String = title(from)

    override fun downloadName(): String = "note-styles.zip"

    override fun read(file: ImportedFile): String =
        if (file.extension == "png") PNG + Base64Text.encode(file.open().use { it.readBytes() }) else file.readText()

    override fun accept(file: ImportedFile): Boolean = file.extension in setOf("png", "xml", "json", "txt")

    override fun classify(file: ImportedFile, text: String): PickedText {
        val label = when (file.extension) {
            "png" -> "Image"
            "xml" -> if (text.contains("<splashes", true)) "Splash config" else "Spritesheet"
            "json" -> when {
                text.contains("\"assets\"") -> "Note style"
                text.contains("\"animations\"") -> "Splash config"
                else -> "Noteskin"
            }
            else -> "Text"
        }
        return PickedText(file.path, file.name, file.path, text, file.extension, label)
    }

    override fun canConvert(items: List<PickedText>): Boolean = items.any { it.kind != "png" }

    override fun build(items: List<PickedText>): ConvertedOutput = convert(items.map { item ->
        if (item.text.startsWith(PNG)) NoteStyleInput(item.path, null, FpsIcon.decode(Base64Text.decode(item.text.substring(PNG.length))))
        else NoteStyleInput(item.path, item.text, null)
    })

    fun convert(inputs: List<NoteStyleInput>): ConvertedOutput {
        val files = NoteStyleFiles(inputs)
        val styles = NoteStyleReaders.read(from, files)
        val report = ReportBuilder()
        if (styles.isEmpty()) {
            val hint = when (from) {
                "vslice" -> "data/notestyles/<id>.json with its images (.png and .xml)"
                "nmv" -> "data/noteskins/<id>.json with its images (.png and .xml)"
                "codename" -> "the note spritesheet (.png and .xml) and data/splashes/<id>.xml"
                else -> "the note skin spritesheet (.png and .xml) and the splash sheet with its .json"
            }
            val missing = if (files.missing.isEmpty()) "" else " Missing: " + files.missing.joinToString(", ") + "."
            throw IllegalArgumentException("No ${title(from)} note skin was found. Add $hint.$missing")
        }
        val outputs = LinkedHashMap<String, OutputFile>()
        val lists = LinkedHashMap<String, MutableList<String>>()
        styles.forEach { style ->
            val result = NoteStyleWriters.write(to, style)
            result.files.forEach { file ->
                if (file.path.endsWith("/list.txt") && file.text != null) {
                    lists.getOrPut(file.path) { mutableListOf() } += file.text.lines().filter { it.isNotBlank() }
                } else outputs[file.path] = OutputFile.of(file.path, file.text, file.bytes)
            }
            result.sections.forEach { (title, lines) -> report.section(title, lines) }
            result.warnings.forEach { report.warn(it) }
        }
        lists.forEach { (path, names) -> outputs[path] = OutputFile.of(path, names.distinct().joinToString("\n") + "\n", null) }
        files.missing.forEach { report.warn("$it was not added, so the parts that use it were left out.") }
        report.stat("Note styles", styles.size)
        report.stat("Images", outputs.keys.count { it.endsWith(".png") })
        val preview = outputs.values.firstOrNull { it.bytes == null && (it.name.endsWith(".json") || it.name.endsWith(".hxc")) }?.content
        return ConvertedOutput(outputs.values.toList(), report.build(), preview)
    }

    companion object {
        const val PNG = "\u0000png:"

        fun title(engine: String): String = when (engine) {
            "vslice" -> "V-Slice"
            "codename" -> "Codename"
            "nmv" -> "NightmareVision"
            "fpsplus" -> "FPS Plus"
            else -> "Psych"
        }
    }
}

object Base64Text {
    private const val ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    private val lookup = IntArray(128) { -1 }.also { table -> ALPHABET.forEachIndexed { i, c -> table[c.code] = i } }

    fun encode(bytes: ByteArray): String {
        val out = StringBuilder((bytes.size + 2) / 3 * 4)
        var i = 0
        while (i < bytes.size) {
            val b0 = bytes[i].toInt() and 0xFF
            val b1 = if (i + 1 < bytes.size) bytes[i + 1].toInt() and 0xFF else 0
            val b2 = if (i + 2 < bytes.size) bytes[i + 2].toInt() and 0xFF else 0
            out.append(ALPHABET[b0 shr 2]).append(ALPHABET[((b0 and 3) shl 4) or (b1 shr 4)])
            out.append(if (i + 1 < bytes.size) ALPHABET[((b1 and 15) shl 2) or (b2 shr 6)] else '=')
            out.append(if (i + 2 < bytes.size) ALPHABET[b2 and 63] else '=')
            i += 3
        }
        return out.toString()
    }

    fun decode(text: String): ByteArray {
        val clean = text.filter { it.code < 128 && lookup[it.code] >= 0 }
        val out = ByteArray(clean.length * 3 / 4)
        var n = 0
        var buffer = 0
        var bits = 0
        clean.forEach { c ->
            buffer = (buffer shl 6) or lookup[c.code]
            bits += 6
            if (bits >= 8) {
                bits -= 8
                out[n++] = (buffer shr bits).toByte()
            }
        }
        return out.copyOf(n)
    }
}
