package com.vconvertercrew.vconverter.script

import kotlinx.cinterop.ByteVar
import kotlinx.cinterop.CPointer
import kotlinx.cinterop.IntVar
import kotlinx.cinterop.ULongVar
import kotlinx.cinterop.addressOf
import kotlinx.cinterop.alloc
import kotlinx.cinterop.convert
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.ptr
import kotlinx.cinterop.readBytes
import kotlinx.cinterop.reinterpret
import kotlinx.cinterop.toKString
import kotlinx.cinterop.usePinned
import kotlinx.cinterop.value
import vscript.vscript_free_bytes
import vscript.vscript_image_bake
import vscript.vscript_image_resize
import vscript.vscript_png_decode
import vscript.vscript_png_encode
import vscript.vscript_builtin_event
import vscript.vscript_convert_data
import vscript.vscript_convert_hx
import vscript.vscript_fix_hx
import vscript.vscript_convert_shader
import vscript.vscript_fix_lua
import vscript.vscript_convert_lua
import vscript.vscript_free
import vscript.vscript_generate

object VScriptNative {
    val available: Boolean = true

    private fun take(result: CPointer<ByteVar>?): String? {
        if (result == null) return null
        return try {
            result.toKString()
        } finally {
            vscript_free(result)
        }
    }

    fun convertLua(source: String, options: String): String? = take(vscript_convert_lua(source, options))

    fun convertHx(source: String, options: String): String? = take(vscript_convert_hx(source, options))

    fun fixLua(source: String, options: String): String? = take(vscript_fix_lua(source, options))

    fun fixHx(source: String, options: String): String? = take(vscript_fix_hx(source, options))
    fun convertShader(source: String, options: String): String? = take(vscript_convert_shader(source, options))

    fun generate(spec: String): String? = take(vscript_generate(spec))

    fun builtinEvent(name: String, className: String, id: String): String? = take(vscript_builtin_event(name, className, id))

    fun convertData(kind: String, input: String): String? = take(vscript_convert_data(kind, input))

    fun pngDecode(png: ByteArray): Triple<Int, Int, ByteArray>? {
        if (png.isEmpty()) return null
        return memScoped {
            val width = alloc<IntVar>()
            val height = alloc<IntVar>()
            val out = png.usePinned { vscript_png_decode(it.addressOf(0).reinterpret(), png.size.convert(), width.ptr, height.ptr) } ?: return@memScoped null
            try {
                Triple(width.value, height.value, out.reinterpret<ByteVar>().readBytes(width.value * height.value * 4))
            } finally {
                vscript_free_bytes(out)
            }
        }
    }

    fun pngEncode(rgba: ByteArray, width: Int, height: Int, level: Int): ByteArray? {
        if (width <= 0 || height <= 0 || rgba.size < width * height * 4) return null
        return memScoped {
            val size = alloc<ULongVar>()
            val out = rgba.usePinned { vscript_png_encode(it.addressOf(0).reinterpret(), width, height, level, size.ptr) } ?: return@memScoped null
            try {
                out.reinterpret<ByteVar>().readBytes(size.value.toInt())
            } finally {
                vscript_free_bytes(out)
            }
        }
    }

    fun imageResize(rgba: ByteArray, width: Int, height: Int, newWidth: Int, newHeight: Int, smooth: Boolean): ByteArray? {
        if (width <= 0 || height <= 0 || newWidth <= 0 || newHeight <= 0 || rgba.size < width * height * 4) return null
        val out = rgba.usePinned { vscript_image_resize(it.addressOf(0).reinterpret(), width, height, newWidth, newHeight, if (smooth) 1 else 0) } ?: return null
        return try {
            out.reinterpret<ByteVar>().readBytes(newWidth * newHeight * 4)
        } finally {
            vscript_free_bytes(out)
        }
    }

    fun imageBake(rgba: ByteArray, width: Int, height: Int, r: Int, g: Int, b: Int): ByteArray? {
        if (width <= 0 || height <= 0 || rgba.size < width * height * 4) return null
        val copy = rgba.copyOf()
        copy.usePinned { vscript_image_bake(it.addressOf(0).reinterpret(), width, height, r.convert(), g.convert(), b.convert()) }
        return copy
    }

    fun convertCharts(difficulties: Array<String>, texts: Array<String>, options: String): Array<String>? = null
}
