package com.vconvertercrew.vconverter.convert.script

import com.vconvertercrew.vconverter.json.JSONObject

data class ScriptCheck(val source: String, val changed: Boolean, val fixes: List<ScriptIssue>, val problems: List<ScriptIssue>) {
    val isClean: Boolean get() = fixes.isEmpty() && problems.isEmpty()

    fun lines(): List<String> = buildList {
        fixes.forEach { add("Fixed" + (if (it.line > 0) " line ${it.line}" else "") + ": " + it.message) }
        problems.forEach { add("Check" + (if (it.line > 0) " line ${it.line}" else "") + ": " + it.message) }
    }
}

object ScriptDoctor {
    val available: Boolean get() = VScriptNative.available

    fun lua(source: String, fileName: String): ScriptCheck? =
        parse(runCatching { VScriptNative.fixLua(source, JSONObject().put("fileName", fileName).toString()) }.getOrNull())

    fun haxe(source: String, fileName: String, engine: String = "vslice", id: String = ""): ScriptCheck? =
        parse(runCatching { VScriptNative.fixHx(source, JSONObject().put("fileName", fileName).put("engine", engine).put("id", id).toString()) }.getOrNull())

    private fun parse(raw: String?): ScriptCheck? {
        if (raw == null) return null
        val result = JSONObject(raw)
        fun list(key: String) = result.optJSONArray(key)?.let { array ->
            (0 until array.length()).mapNotNull { array.optJSONObject(it) }.map { ScriptIssue(it.optInt("line"), it.optString("message")) }
        }.orEmpty()
        return ScriptCheck(result.optString("source"), result.optBoolean("changed"), list("fixes"), list("problems"))
    }
}
