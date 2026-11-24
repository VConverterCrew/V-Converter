package com.vconvertercrew.vconverter.script

import com.vconvertercrew.vconverter.convert.ConvertedOutput
import com.vconvertercrew.vconverter.convert.PickedText
import com.vconvertercrew.vconverter.core.Report
import com.vconvertercrew.vconverter.core.ReportBuilder
import com.vconvertercrew.vconverter.io.ImportedFile
import com.vconvertercrew.vconverter.io.OutputFile
import com.vconvertercrew.vconverter.json.JSONObject

data class ShaderResult(val source: String, val changed: Boolean, val fixes: List<ScriptIssue>, val problems: List<ScriptIssue>, val uniforms: List<String>) {
    fun lines(): List<String> = buildList {
        if (uniforms.isNotEmpty()) add("Uniforms: ${uniforms.joinToString()}")
        fixes.forEach { add("Fixed" + (if (it.line > 0) " line ${it.line}" else "") + ": " + it.message) }
        problems.forEach { add("Check: " + it.message) }
    }
}

object ShaderConverter {
    val extensions = setOf("frag", "vert", "glsl", "fs", "vs", "fsh", "vsh")

    fun isShader(path: String): Boolean = path.substringAfterLast('.', "").lowercase() in extensions

    fun accepts(file: ImportedFile): Boolean = file.extension in extensions

    fun isVertex(path: String): Boolean = path.substringAfterLast('.', "").lowercase() in setOf("vert", "vs", "vsh")

    fun convert(source: String, name: String, from: String, to: String, vertex: Boolean = false): ShaderResult {
        val options = JSONObject().put("from", from).put("to", to).put("name", name).put("vertex", vertex)
        val raw = runCatching { VScriptNative.convertShader(source, options.toString()) }.getOrNull()
            ?: return ShaderResult(source, false, emptyList(), emptyList(), emptyList())
        val json = JSONObject(raw)
        fun list(key: String) = json.optJSONArray(key)?.let { a ->
            (0 until a.length()).mapNotNull { a.optJSONObject(it) }.map { ScriptIssue(it.optInt("line"), it.optString("message")) }
        }.orEmpty()
        val uniforms = json.optJSONArray("uniforms")?.let { a -> (0 until a.length()).map { a.optString(it) } }.orEmpty()
        return ShaderResult(json.optString("source"), json.optBoolean("changed"), list("fixes"), list("problems"), uniforms)
    }

    fun folder(to: String): String = if (to == "fpsplus") "data/shaders" else "shaders"

    fun targetPath(path: String, to: String): String {
        val name = path.substringAfterLast('/')
        val stem = name.substringBeforeLast('.')
        val ext = if (isVertex(name)) "vert" else "frag"
        return "${folder(to)}/$stem.$ext"
    }

    fun pick(file: ImportedFile, text: String): PickedText =
        PickedText(file.path, file.name, file.path, text, "shader", if (isVertex(file.name)) "Vertex shader" else "Fragment shader")

    fun build(items: List<PickedText>, from: String, to: String): Pair<List<OutputFile>, Report> {
        val report = ReportBuilder()
        val outputs = items.map { item ->
            val result = convert(item.text, item.name.substringBeforeLast('.'), from, to, isVertex(item.name))
            val path = targetPath(item.path, to)
            report.section("${item.name} → $path", result.lines().ifEmpty { listOf("Already works as a FlxRuntimeShader; copied as it is.") })
            OutputFile.of(path, result.source, null)
        }
        if (items.isNotEmpty()) report.stat("Shaders", items.size)
        return outputs to report.build()
    }

    fun merge(items: List<PickedText>, from: String, to: String, convert: (List<PickedText>) -> ConvertedOutput): ConvertedOutput {
        val shaders = items.filter { it.kind == "shader" }
        val rest = items.filter { it.kind != "shader" }
        val (shaderFiles, shaderReport) = build(shaders, from, to)
        if (rest.isEmpty()) {
            require(shaderFiles.isNotEmpty()) { "Nothing to convert." }
            return ConvertedOutput(shaderFiles, shaderReport, shaderFiles.singleOrNull()?.content)
        }
        val main = convert(rest)
        return ConvertedOutput(main.files + shaderFiles, main.report + shaderReport, main.preview)
    }
}
