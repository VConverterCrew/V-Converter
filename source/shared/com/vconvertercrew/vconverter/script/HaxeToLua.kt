package com.vconvertercrew.vconverter.script

import com.vconvertercrew.vconverter.json.JSONArray
import com.vconvertercrew.vconverter.json.JSONObject

data class HaxeLuaScript(
    val source: String,
    val kind: String,
    val className: String,
    val targetId: String,
    val issues: List<ScriptIssue>,
    val callbacks: List<String>,
    val fallbacks: Int
) {
    fun notes(): List<String> = buildList {
        add(
            if (fallbacks == 0) "Converted to Psych Lua."
            else "Converted to Psych Lua; $fallbacks statement(s) had no Lua equivalent and run as Haxe inside runHaxeCode()."
        )
        issues.forEach { add(if (it.line > 0) "Line ${it.line}: ${it.message}" else it.message) }
    }
}

object HaxeToLua {
    val available: Boolean get() = VScriptNative.available

    val luaKinds = setOf("module", "song", "stage", "event", "notekind", "character", "script")

    fun luaPath(path: String): String = path.substringBeforeLast('.') + ".lua"

    fun convert(
        source: String,
        engine: String,
        fileName: String,
        id: String = "",
        stageProps: Collection<String> = emptyList(),
        eventName: String = "",
        noteType: String = ""
    ): HaxeLuaScript? {
        if (!available) return null
        val options = JSONObject()
            .put("engine", engine)
            .put("fileName", fileName)
            .put("id", id)
            .put("eventName", eventName)
            .put("noteType", noteType)
            .put("stageProps", JSONArray().also { array -> stageProps.forEach { array.put(it) } })
        val raw = runCatching { VScriptNative.convertHx(source, options.toString()) }.getOrNull() ?: return null
        val result = JSONObject(raw)
        if (!result.optBoolean("ok")) return null
        val kind = result.optString("kind")
        if (kind !in luaKinds) return null
        val issues = result.optJSONArray("issues")?.let { array ->
            (0 until array.length()).mapNotNull { array.optJSONObject(it) }.map { ScriptIssue(it.optInt("line"), it.optString("message")) }
        }.orEmpty()
        val callbacks = result.optJSONArray("callbacks")?.let { array -> (0 until array.length()).map { array.optString(it) } }.orEmpty()
        return HaxeLuaScript(
            result.optString("source"),
            kind,
            result.optString("className"),
            result.optString("targetId"),
            issues,
            callbacks,
            result.optInt("fallbacks")
        )
    }
}
