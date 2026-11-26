package com.vconvertercrew.vconverter.engines.luaslice

import com.vconvertercrew.vconverter.convert.mod.ModConversionResult
import com.vconvertercrew.vconverter.convert.mod.ModConverter
import com.vconvertercrew.vconverter.convert.mod.ModFiles
import com.vconvertercrew.vconverter.convert.mod.ModInfo
import com.vconvertercrew.vconverter.convert.mod.ModProgress
import com.vconvertercrew.vconverter.convert.mod.ModScan
import com.vconvertercrew.vconverter.convert.mod.ModWriter
import com.vconvertercrew.vconverter.convert.mod.readText
import com.vconvertercrew.vconverter.convert.reverse.VSliceModConverter
import com.vconvertercrew.vconverter.convert.reverse.VSliceModScan
import com.vconvertercrew.vconverter.convert.reverse.VSliceModScanner
import com.vconvertercrew.vconverter.convert.reverse.VSliceToPsychChart
import com.vconvertercrew.vconverter.core.ReportBuilder
import com.vconvertercrew.vconverter.engines.codename.CodenameModConverter
import com.vconvertercrew.vconverter.engines.codename.CodenameModScan

data class LuaSliceScan(val base: VSliceModScan, val psychScripts: List<String>, val nativeScripts: List<String>) {
    val isEmpty: Boolean get() = base.isEmpty && psychScripts.isEmpty()
}

object LuaSliceModScanner {
    fun looksLikeLuaSlice(files: ModFiles): Boolean = files.paths.any { LuaSlice.isPsychScript(it) } || VSliceModScanner.looksLikeVSlice(files)

    fun scan(files: ModFiles): LuaSliceScan {
        val base = VSliceModScanner.scan(files)
        val inside = files.paths.filter { it.startsWith(base.root) && it !in base.placeholders.values }
        val psych = inside.filter { LuaSlice.isPsychScript(it) }
        val native = inside.filter { LuaSlice.isNativeScript(it) }
        val scripts = (psych + native).map { base.relative(it) }.toSet()
        return LuaSliceScan(base.copy(ignored = base.ignored.filter { it !in scripts }), psych, native)
    }

    fun readInfo(files: ModFiles, scan: LuaSliceScan): ModInfo? = VSliceModScanner.readInfo(files, scan.base)
}

object ToLuaSliceMod {
    private fun withNotes(result: ModConversionResult, notes: List<String>): ModConversionResult {
        val extra = ReportBuilder()
        extra.section(LuaSlice.TITLE, notes)
        return ModConversionResult(result.filesWritten, result.report + extra.build())
    }

    fun fromPsych(files: ModFiles, scan: ModScan, writer: ModWriter, info: ModInfo, progress: (ModProgress) -> Unit = {}): ModConversionResult =
        withNotes(ModConverter(files, scan, writer, progress, luaSlice = true).convert(info), LuaSlice.notes)

    fun fromCodename(files: ModFiles, scan: CodenameModScan, writer: ModWriter, info: ModInfo, progress: (ModProgress) -> Unit = {}): ModConversionResult =
        withNotes(CodenameModConverter(files, scan, writer, progress).convert(info), listOf("LuaSlice runs V-Slice mods as they are, so this is the V-Slice conversion; HScript became .hxc scripts."))
}

object LuaSliceToPsych {
    fun convert(files: ModFiles, scan: LuaSliceScan, writer: ModWriter, info: ModInfo, progress: (ModProgress) -> Unit = {}): ModConversionResult {
        val base = VSliceModConverter(files, scan.base, writer, progress).convert(info)
        val report = ReportBuilder()
        var written = 0
        val lines = mutableListOf<String>()
        scan.psychScripts.forEach { path ->
            val rel = scan.base.relative(path)
            try {
                val target = LuaSlice.toPsychPath(rel) { VSliceToPsychChart.songIdFor(it, "") }
                writer.writeText(target, LuaSlice.toPsychText(files.readText(path)))
                written++
                lines += "$rel → $target"
            } catch (e: Exception) {
                report.warn("Script $rel was not copied: ${e.message}")
            }
        }
        if (lines.isNotEmpty()) {
            report.stat("Lua scripts kept", lines.size)
            report.section("Psych Lua scripts", if (lines.size <= 40) lines else lines.take(40) + "…and ${lines.size - 40} more")
        }
        if (scan.nativeScripts.isNotEmpty()) {
            report.warn("${scan.nativeScripts.size} LuaSlice .lua/.luag scripts use LuaSlice's own Lua API and were not converted: ${scan.nativeScripts.take(8).joinToString { scan.base.relative(it) }}${if (scan.nativeScripts.size > 8) "…" else ""}")
        }
        report.section(LuaSlice.TITLE, listOf(
            ".luap and .luapg scripts became Psych .lua scripts: mod-root .luapg files went to scripts/, and data/songs/<song>/ scripts went to data/<song>/.",
            "Charts keep LuaSlice's Psych event names and values, and ChangeCharacter events became Change Character."
        ))
        return ModConversionResult(base.filesWritten + written, base.report + report.build())
    }
}
