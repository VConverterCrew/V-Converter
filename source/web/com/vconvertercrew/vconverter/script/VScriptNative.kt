package com.vconvertercrew.vconverter.script

object VScriptNative {
    val available: Boolean = false

    fun convertLua(source: String, options: String): String? = null

    fun convertHx(source: String, options: String): String? = null

    fun fixLua(source: String, options: String): String? = null

    fun fixHx(source: String, options: String): String? = null

    fun generate(spec: String): String? = null

    fun builtinEvent(name: String, className: String, id: String): String? = null

    fun convertData(kind: String, input: String): String? = null

    fun convertCharts(difficulties: Array<String>, texts: Array<String>, options: String): Array<String>? = null
}
