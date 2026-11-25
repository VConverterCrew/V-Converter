package com.vconvertercrew.vconverter.launcher

import com.vconvertercrew.vconverter.convert.mod.ModConverter
import com.vconvertercrew.vconverter.convert.mod.ModWriter
import com.vconvertercrew.vconverter.platform.InputStream

class FunkinModWriter(private val target: ModWriter, private val prefix: String, private val game: LaunchGame = LaunchGame.FUNKIN) : ModWriter {
    var count = 0
        private set
    private var hasMeta = false

    override fun writeText(path: String, text: String) {
        target.writeText(prefix + mark(path), text)
    }

    override fun copy(path: String, source: () -> InputStream) {
        target.copy(prefix + mark(path), source)
    }

    private fun mark(path: String): String {
        val clean = path.replace('\\', '/').trimStart('/')
        synchronized(this) {
            count++
            if (clean.equals(game.metaFile, ignoreCase = true)) hasMeta = true
        }
        return clean
    }

    fun finish(title: String) {
        if (!hasMeta) writeText(game.metaFile, if (game == LaunchGame.PSYCH) psychPack(title) else metadata(title))
    }

    companion object {
        const val META = "_polymod_meta.json"

        private fun quote(text: String): String = buildString {
            append('"')
            text.forEach { c ->
                when {
                    c == '"' -> append("\\\"")
                    c == '\\' -> append("\\\\")
                    c == '\n' -> append("\\n")
                    c == '\r' -> append("\\r")
                    c == '\t' -> append("\\t")
                    c < ' ' -> append("\\u%04x".format(c.code))
                    else -> append(c)
                }
            }
            append('"')
        }

        fun metadata(title: String): String =
            "{\n  \"title\": ${quote(title)},\n  \"description\": \"Converted with V-Converter.\",\n  \"contributors\": [],\n  \"homepage\": \"\",\n  \"api_version\": \"${ModConverter.API_VERSION}\",\n  \"mod_version\": \"1.0.0\",\n  \"license\": \"\"\n}"

        fun psychPack(title: String): String = com.vconvertercrew.vconverter.engines.psych.PsychWriters.pack(title.ifBlank { "Converted Mod" }, "Converted with V-Converter.")

        fun prefix(name: String, insideMods: Boolean): String = if (insideMods) "$name/" else "mods/$name/"
    }
}
