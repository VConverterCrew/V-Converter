package com.vconvertercrew.vconverter.script

object VScriptNative {
    val available: Boolean = false

    fun convertLua(source: String, options: String): String? = null

    fun convertHx(source: String, options: String): String? = null

    fun fixLua(source: String, options: String): String? = null

    fun fixHx(source: String, options: String): String? = null
    fun convertShader(source: String, options: String): String? = null

    fun generate(spec: String): String? = null

    fun builtinEvent(name: String, className: String, id: String): String? = null

    fun convertData(kind: String, input: String): String? = null

    fun pngDecode(png: ByteArray): Triple<Int, Int, ByteArray>? = null

    fun pngEncode(rgba: ByteArray, width: Int, height: Int, level: Int): ByteArray? = null

    fun imageResize(rgba: ByteArray, width: Int, height: Int, newWidth: Int, newHeight: Int, smooth: Boolean): ByteArray? = null

    fun imageBake(rgba: ByteArray, width: Int, height: Int, r: Int, g: Int, b: Int): ByteArray? = null

    fun convertFiles(kind: String, names: Array<String>, texts: Array<String>, options: String): Array<String>? = null

    fun convertCharts(difficulties: Array<String>, texts: Array<String>, options: String): Array<String>? = null
}
