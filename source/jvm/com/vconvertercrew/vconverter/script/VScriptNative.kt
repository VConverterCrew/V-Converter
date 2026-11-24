package com.vconvertercrew.vconverter.script

import com.vconvertercrew.vconverter.NativeLibraries

object VScriptNative {
    private const val LIBRARY = "vconverter_script"

    var loadError: String? = null
        private set

    val available: Boolean by lazy {
        val result = runCatching {
            NativeLibraries.load(LIBRARY, "vconverter.script.library")
            nativeVersion()
        }
        loadError = result.exceptionOrNull()?.let { it.message ?: it::class.java.simpleName }
        result.isSuccess
    }

    fun convertLua(source: String, options: String): String? = if (available) nativeConvertLua(source, options) else null

    fun convertHx(source: String, options: String): String? = if (available) nativeConvertHx(source, options) else null

    fun fixLua(source: String, options: String): String? = if (available) nativeFixLua(source, options) else null

    fun fixHx(source: String, options: String): String? = if (available) nativeFixHx(source, options) else null
    fun convertShader(source: String, options: String): String? = if (available) nativeConvertShader(source, options) else null

    fun generate(spec: String): String? = if (available) nativeGenerate(spec) else null

    fun builtinEvent(name: String, className: String, id: String): String? = if (available) nativeBuiltinEvent(name, className, id) else null

    fun convertData(kind: String, input: String): String? = if (available) nativeConvertData(kind, input) else null

    fun convertFiles(kind: String, names: Array<String>, texts: Array<String>, options: String): Array<String>? =
        if (available) nativeConvertFiles(kind, names, texts, options) else null

    fun convertCharts(difficulties: Array<String>, texts: Array<String>, options: String): Array<String>? =
        if (available) nativeConvertCharts(difficulties, texts, options) else null

    fun pngDecode(png: ByteArray): Triple<Int, Int, ByteArray>? {
        if (!available) return null
        val out = nativePngDecode(png) ?: return null
        fun int(at: Int) = ((out[at].toInt() and 0xFF) shl 24) or ((out[at + 1].toInt() and 0xFF) shl 16) or ((out[at + 2].toInt() and 0xFF) shl 8) or (out[at + 3].toInt() and 0xFF)
        return Triple(int(0), int(4), out.copyOfRange(8, out.size))
    }

    fun pngEncode(rgba: ByteArray, width: Int, height: Int, level: Int): ByteArray? = if (available) nativePngEncode(rgba, width, height, level) else null

    fun imageResize(rgba: ByteArray, width: Int, height: Int, newWidth: Int, newHeight: Int, smooth: Boolean): ByteArray? =
        if (available) nativeImageResize(rgba, width, height, newWidth, newHeight, smooth) else null

    fun imageBake(rgba: ByteArray, width: Int, height: Int, r: Int, g: Int, b: Int): ByteArray? {
        if (!available) return null
        val copy = rgba.copyOf()
        return if (nativeImageBake(copy, width, height, r, g, b)) copy else null
    }

    @JvmStatic private external fun nativePngDecode(png: ByteArray): ByteArray?
    @JvmStatic private external fun nativePngEncode(rgba: ByteArray, width: Int, height: Int, level: Int): ByteArray?
    @JvmStatic private external fun nativeImageResize(rgba: ByteArray, width: Int, height: Int, newWidth: Int, newHeight: Int, smooth: Boolean): ByteArray?
    @JvmStatic private external fun nativeImageBake(rgba: ByteArray, width: Int, height: Int, r: Int, g: Int, b: Int): Boolean
    @JvmStatic private external fun nativeVersion(): String
    @JvmStatic private external fun nativeConvertLua(source: String, options: String): String?
    @JvmStatic private external fun nativeConvertHx(source: String, options: String): String?
    @JvmStatic private external fun nativeFixLua(source: String, options: String): String?
    @JvmStatic private external fun nativeFixHx(source: String, options: String): String?
    @JvmStatic private external fun nativeConvertShader(source: String, options: String): String?
    @JvmStatic private external fun nativeGenerate(spec: String): String?
    @JvmStatic private external fun nativeBuiltinEvent(name: String, className: String, id: String): String?
    @JvmStatic private external fun nativeConvertData(kind: String, input: String): String?
    @JvmStatic private external fun nativeConvertFiles(kind: String, names: Array<String>, texts: Array<String>, options: String): Array<String>?
    @JvmStatic private external fun nativeConvertCharts(difficulties: Array<String>, texts: Array<String>, options: String): Array<String>?
}
