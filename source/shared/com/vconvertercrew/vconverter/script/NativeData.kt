package com.vconvertercrew.vconverter.script

import com.vconvertercrew.vconverter.core.Report
import com.vconvertercrew.vconverter.core.ReportBuilder
import com.vconvertercrew.vconverter.json.JSONArray
import com.vconvertercrew.vconverter.json.JSONObject

object NativeData {
    var enabled: Boolean = true

    var conversions: Int = 0
        private set

    fun convert(kind: String, input: JSONObject): JSONObject? {
        if (!enabled || !VScriptNative.available) return null
        val raw = runCatching { VScriptNative.convertData(kind, input.toString()) }.getOrNull() ?: return null
        val result = runCatching { JSONObject(raw) }.getOrNull() ?: return null
        if (!result.optBoolean("ok")) return null
        return result.optJSONObject("result")?.also { conversions++ }
    }

    fun convertCharts(difficulties: List<String>, texts: List<String>, options: JSONObject): Pair<JSONObject, List<String>>? {
        if (!enabled || !VScriptNative.available) return null
        val raw = runCatching { VScriptNative.convertCharts(difficulties.toTypedArray(), texts.toTypedArray(), options.toString()) }.getOrNull() ?: return null
        if (raw.isEmpty()) return null
        val header = runCatching { JSONObject(raw[0]) }.getOrNull() ?: return null
        if (!header.optBoolean("ok")) return null
        conversions++
        return header to raw.drop(1)
    }

    fun convertFiles(kind: String, inputs: List<Pair<String, String>>, options: JSONObject): Pair<JSONObject, List<Pair<String, String>>>? {
        if (!enabled || !VScriptNative.available) return null
        val raw = runCatching { VScriptNative.convertFiles(kind, inputs.map { it.first }.toTypedArray(), inputs.map { it.second }.toTypedArray(), options.toString()) }.getOrNull() ?: return null
        if (raw.isEmpty()) return null
        val header = runCatching { JSONObject(raw[0]) }.getOrNull() ?: return null
        if (!header.optBoolean("ok")) return null
        val paths = strings(header.optJSONArray("files"))
        if (paths.size != raw.size - 1) return null
        conversions++
        return (header.optJSONObject("result") ?: JSONObject()) to paths.zip(raw.drop(1))
    }

    fun jsonKeys(text: String): Map<String, String>? {
        val (result, _) = convertFiles("json-keys", listOf("" to text), JSONObject()) ?: return null
        val entry = result.optJSONArray("entries")?.optJSONObject(0) ?: return null
        if (!entry.optBoolean("valid")) return emptyMap()
        val keys = entry.optJSONArray("keys") ?: return emptyMap()
        return (0 until keys.length()).mapNotNull { keys.optJSONArray(it) }.associate { it.optString(0) to it.optString(1) }
    }

    fun report(json: JSONObject?): Report {
        val builder = ReportBuilder()
        if (json == null) return builder.build()
        json.optJSONArray("stats")?.let { stats ->
            for (i in 0 until stats.length()) {
                val stat = stats.optJSONArray(i) ?: continue
                builder.stat(stat.optString(0), stat.opt(1) ?: "")
            }
        }
        json.optJSONArray("sections")?.let { sections ->
            for (i in 0 until sections.length()) {
                val section = sections.optJSONArray(i) ?: continue
                builder.section(section.optString(0), strings(section.optJSONArray(1)))
            }
        }
        strings(json.optJSONArray("warnings")).forEach(builder::warn)
        return builder.build()
    }

    fun strings(array: JSONArray?): List<String> = if (array == null) emptyList() else (0 until array.length()).map { array.optString(it) }
}
