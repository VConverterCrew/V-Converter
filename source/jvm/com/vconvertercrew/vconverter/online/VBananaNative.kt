package com.vconvertercrew.vconverter.online

import com.vconvertercrew.vconverter.NativeLibraries
import java.io.File

object VBananaNative {
    private const val LIBRARY = "vconverter_banana"

    var loadError: String? = null
        private set

    val available: Boolean by lazy {
        val result = runCatching {
            NativeLibraries.load(LIBRARY, "vconverter.banana.library")
            nativeVersion()
        }
        loadError = result.exceptionOrNull()?.let { it.message ?: it::class.java.simpleName }
        result.isSuccess
    }

    fun request(json: String): String? = if (available) nativeRequest(json.encodeToByteArray())?.decodeToString() else null

    fun parse(kind: String, body: String, options: String = "{}"): String? =
        if (available) nativeParse(kind.encodeToByteArray(), body.encodeToByteArray(), options.encodeToByteArray())?.decodeToString() else null

    fun link(text: String): String? = if (available) nativeLink(text.encodeToByteArray())?.decodeToString() else null

    fun inspect(entries: String): String? = if (available) nativeInspect(entries.encodeToByteArray())?.decodeToString() else null

    fun md5File(path: String): String? = if (available) nativeMd5File(File(path).absolutePath.encodeToByteArray()) else null

    @JvmStatic private external fun nativeVersion(): String
    @JvmStatic private external fun nativeRequest(request: ByteArray): ByteArray?
    @JvmStatic private external fun nativeParse(kind: ByteArray, body: ByteArray, options: ByteArray): ByteArray?
    @JvmStatic private external fun nativeLink(text: ByteArray): ByteArray?
    @JvmStatic private external fun nativeInspect(entries: ByteArray): ByteArray?
    @JvmStatic private external fun nativeMd5File(path: ByteArray): String?
}
