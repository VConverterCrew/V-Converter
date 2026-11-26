package com.vconvertercrew.vconverter.convert

import com.vconvertercrew.vconverter.convert.reverse.VSliceToPsychChart
import com.vconvertercrew.vconverter.core.Report
import com.vconvertercrew.vconverter.core.ReportBuilder
import com.vconvertercrew.vconverter.engines.luaslice.LuaSlice
import com.vconvertercrew.vconverter.io.ImportedFile
import com.vconvertercrew.vconverter.io.OutputFile

class LuaSliceTargetConverter(private val inner: FileConverter) : FileConverter() {
    override val noun: String get() = inner.noun
    override val engine: String get() = inner.engine

    override fun downloadName(): String = inner.downloadName().substringBeforeLast('.').removeSuffix("-vslice").removeSuffix("-luaslice") + "-luaslice.zip"

    override fun read(file: ImportedFile): String = inner.read(file)

    override fun accept(file: ImportedFile): Boolean = inner.accept(file)

    override fun classify(file: ImportedFile, text: String): PickedText = inner.classify(file, text)

    override fun canConvert(items: List<PickedText>): Boolean = inner.canConvert(items)

    override fun build(items: List<PickedText>): ConvertedOutput = inner.build(items)
}

class PsychToLuaSliceScriptsConverter : FileConverter() {
    private val hscript = PsychScriptsConverter()

    override val noun = "script"
    override val engine = LuaSlice.TITLE

    override fun downloadName() = "scripts-luaslice.zip"

    override fun accept(file: ImportedFile) = file.extension == "lua" || hscript.accept(file)

    override fun classify(file: ImportedFile, text: String): PickedText {
        if (file.extension != "lua") return hscript.classify(file, text)
        val target = LuaSlice.toLuaSlicePath(LuaSlice.modRelative(file.path))
        return PickedText(file.path, file.name, file.path, text, "lua", "Psych Lua · $target")
    }

    override fun build(items: List<PickedText>): ConvertedOutput {
        val lua = items.filter { it.kind == "lua" }
        val others = items.filter { it.kind != "lua" }
        val files = mutableListOf<OutputFile>()
        var report: Report = ReportBuilder().build()
        var preview: String? = null
        if (lua.isNotEmpty()) {
            val lines = lua.map { item ->
                val target = LuaSlice.toLuaSlicePath(LuaSlice.modRelative(item.path))
                val text = LuaSlice.toLuaSliceText(item.text)
                files += out(target, text)
                if (preview == null) preview = text
                "${item.name} → $target"
            }
            val built = ReportBuilder()
            built.stat("Lua scripts kept", lua.size)
            built.section("LuaSlice Psych scripts", lines)
            built.section(LuaSlice.TITLE, LuaSlice.notes.take(1))
            report = built.build()
        }
        if (others.isNotEmpty()) {
            val out = hscript.build(others)
            files += out.files
            report += out.report
            if (preview == null) preview = out.preview
        }
        return ConvertedOutput(files, report, preview)
    }
}

class LuaSliceToPsychScriptsConverter : FileConverter() {
    private val hscript = ReverseScriptsConverter()

    override val noun = "script"
    override val engine = "Psych Engine"

    override fun downloadName() = "scripts-psych.zip"

    override fun accept(file: ImportedFile) = file.extension in LuaSlice.psychExtensions || hscript.accept(file)

    override fun classify(file: ImportedFile, text: String): PickedText {
        if (file.extension !in LuaSlice.psychExtensions) return hscript.classify(file, text)
        val target = target(file.path)
        return PickedText(file.path, file.name, file.path, text, "luap", "LuaSlice Psych Lua · $target")
    }

    private fun target(path: String) = LuaSlice.toPsychPath(LuaSlice.modRelative(path)) { VSliceToPsychChart.songIdFor(it, "") }

    override fun build(items: List<PickedText>): ConvertedOutput {
        val lua = items.filter { it.kind == "luap" }
        val others = items.filter { it.kind != "luap" }
        val files = mutableListOf<OutputFile>()
        var report: Report = ReportBuilder().build()
        var preview: String? = null
        if (lua.isNotEmpty()) {
            val lines = lua.map { item ->
                val target = target(item.path)
                val text = LuaSlice.toPsychText(item.text)
                files += out(target, text)
                if (preview == null) preview = text
                "${item.name} → $target"
            }
            val built = ReportBuilder()
            built.stat("Lua scripts kept", lua.size)
            built.section("Psych Lua scripts", lines)
            report = built.build()
        }
        if (others.isNotEmpty()) {
            val out = hscript.build(others)
            files += out.files
            report += out.report
            if (preview == null) preview = out.preview
        }
        return ConvertedOutput(files, report, preview)
    }
}
