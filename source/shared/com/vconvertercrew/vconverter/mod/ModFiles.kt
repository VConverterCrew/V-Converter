package com.vconvertercrew.vconverter.mod

import com.vconvertercrew.vconverter.platform.InputStream
import com.vconvertercrew.vconverter.platform.OutputStream

interface ModFiles {
    val paths: List<String>
    fun open(path: String): InputStream
    fun size(path: String): Long
}

fun ModFiles.readText(path: String): String = open(path).use { it.readBytes().decodeToString() }

interface ModWriter {
    fun writeText(path: String, text: String)
    fun copy(path: String, source: () -> InputStream)
    fun copyFrom(path: String, files: ModFiles, source: String) = copy(path) { files.open(source) }
}

object SafePaths {
    fun normalize(raw: String): String? {
        val parts = raw.replace('\\', '/').split('/').filter { it.isNotEmpty() && it != "." }
        if (parts.isEmpty() || parts.any { it == ".." } || parts.any { it.contains('\u0000') }) return null
        return parts.joinToString("/")
    }

    fun copyStream(input: InputStream, output: OutputStream): Long = input.copyTo(output, 64 * 1024)
}

fun ModWriter.copyAsset(target: String, files: ModFiles, source: String, from: String, to: String): List<String>? {
    if (!target.startsWith("shaders/") || !com.vconvertercrew.vconverter.script.ShaderConverter.isShader(target)) {
        copyFrom(target, files, source)
        return null
    }
    val result = com.vconvertercrew.vconverter.script.ShaderConverter.convert(
        files.readText(source), target.substringAfterLast('/').substringBeforeLast('.'), from, to,
        com.vconvertercrew.vconverter.script.ShaderConverter.isVertex(target)
    )
    writeText(com.vconvertercrew.vconverter.script.ShaderConverter.targetPath(target, to), result.source)
    return result.lines().takeIf { result.changed || result.problems.isNotEmpty() }
}
