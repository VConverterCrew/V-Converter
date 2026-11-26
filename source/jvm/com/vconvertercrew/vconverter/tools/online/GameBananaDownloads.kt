package com.vconvertercrew.vconverter.tools.online

import java.io.File
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicLong

object GameBananaDownloads {
    private const val TIMEOUT_MS = 20000
    private const val AGENT = "V-Converter"
    private const val SPLIT_FROM = 4L * 1024 * 1024
    private const val MAX_PARTS = 6

    private class Probe(val url: String, val total: Long, val ranges: Boolean)

    private fun open(url: String, range: String?): HttpURLConnection {
        val connection = URL(url).openConnection() as HttpURLConnection
        connection.instanceFollowRedirects = false
        connection.connectTimeout = TIMEOUT_MS
        connection.readTimeout = TIMEOUT_MS * 3
        connection.setRequestProperty("User-Agent", AGENT)
        connection.setRequestProperty("Accept-Encoding", "identity")
        if (range != null) connection.setRequestProperty("Range", range)
        return connection
    }

    private fun follow(start: String, range: String?): Pair<String, HttpURLConnection> {
        var url = start
        repeat(8) {
            val connection = open(url, range)
            val status = connection.responseCode
            if (status in 300..399) {
                val next = connection.getHeaderField("Location")
                connection.disconnect()
                if (next == null) throw GameBananaException("GameBanana sent a broken redirect.")
                url = URL(URL(url), next).toString()
                return@repeat
            }
            return url to connection
        }
        throw GameBananaException("GameBanana redirected too many times.")
    }

    private fun probe(url: String, fallbackSize: Long): Probe {
        val (finalUrl, connection) = follow(url, "bytes=0-0")
        try {
            val status = connection.responseCode
            if (status == 206) {
                val total = connection.getHeaderField("Content-Range")?.substringAfterLast('/')?.trim()?.toLongOrNull() ?: -1L
                return Probe(finalUrl, if (total > 0) total else fallbackSize, total > 0)
            }
            if (status !in 200..299) throw GameBananaException("The download failed ($status).")
            return Probe(finalUrl, connection.contentLengthLong.takeIf { it > 0 } ?: fallbackSize, false)
        } finally {
            connection.disconnect()
        }
    }

    private fun single(url: String, partial: File, expected: Long, progress: (Float) -> Unit): Long {
        val (_, connection) = follow(url, null)
        try {
            val status = connection.responseCode
            if (status !in 200..299) throw GameBananaException("The download failed ($status).")
            val total = connection.contentLengthLong.takeIf { it > 0 } ?: expected
            var done = 0L
            connection.inputStream.use { input ->
                partial.outputStream().buffered(1 shl 20).use { output ->
                    val buffer = ByteArray(256 * 1024)
                    while (true) {
                        val read = input.read(buffer)
                        if (read < 0) break
                        output.write(buffer, 0, read)
                        done += read
                        if (total > 0) progress((done.toFloat() / total).coerceAtMost(0.99f))
                    }
                }
            }
            return done
        } finally {
            connection.disconnect()
        }
    }

    private fun part(url: String, file: RandomAccessFile, from: Long, to: Long, done: AtomicLong, total: Long, progress: (Float) -> Unit) {
        var position = from
        var attempts = 0
        while (position <= to) {
            val connection = open(url, "bytes=$position-$to")
            try {
                val status = connection.responseCode
                if (status != 206) throw GameBananaException("The download failed ($status).")
                connection.inputStream.use { input ->
                    val buffer = ByteArray(256 * 1024)
                    while (position <= to) {
                        val read = input.read(buffer, 0, minOf(buffer.size.toLong(), to - position + 1).toInt())
                        if (read < 0) break
                        synchronized(file) {
                            file.seek(position)
                            file.write(buffer, 0, read)
                        }
                        position += read
                        progress((done.addAndGet(read.toLong()).toFloat() / total).coerceAtMost(0.99f))
                    }
                }
            } catch (e: Exception) {
                if (++attempts > 3) throw e
            } finally {
                connection.disconnect()
            }
        }
    }

    private fun split(probe: Probe, partial: File, progress: (Float) -> Unit): Long {
        val parts = minOf(MAX_PARTS.toLong(), maxOf(2L, probe.total / (2L * 1024 * 1024))).toInt()
        val size = (probe.total + parts - 1) / parts
        val done = AtomicLong()
        RandomAccessFile(partial, "rw").use { file ->
            file.setLength(probe.total)
            val pool = Executors.newFixedThreadPool(parts)
            try {
                val jobs = (0 until parts).map { index ->
                    val from = index * size
                    val to = minOf(probe.total - 1, from + size - 1)
                    pool.submit { if (from <= to) part(probe.url, file, from, to, done, probe.total, progress) }
                }
                jobs.forEach { job ->
                    try {
                        job.get()
                    } catch (e: java.util.concurrent.ExecutionException) {
                        throw e.cause ?: e
                    }
                }
            } finally {
                pool.shutdownNow()
            }
        }
        return done.get()
    }

    fun toFile(file: GameBananaFile, folder: File, progress: (Float) -> Unit): File {
        folder.mkdirs()
        folder.listFiles()?.forEach { it.deleteRecursively() }
        val target = File(folder, GameBanana.safeFileName(file))
        val partial = File(folder, target.name + ".part")
        val probe = runCatching { probe(file.url, file.size) }.getOrNull()
        val written = if (probe != null && probe.ranges && probe.total >= SPLIT_FROM) {
            runCatching { split(probe, partial, progress) }.getOrElse {
                partial.delete()
                single(probe.url, partial, probe.total, progress)
            }
        } else {
            single(probe?.url ?: file.url, partial, probe?.total ?: file.size, progress)
        }
        if (written == 0L) throw GameBananaException("GameBanana sent an empty file.")
        if (file.md5.length == 32 && VBananaNative.available) {
            val sum = VBananaNative.md5File(partial.absolutePath)
            if (sum != null && !sum.equals(file.md5, ignoreCase = true)) {
                partial.delete()
                throw GameBananaException("The download is damaged (its MD5 doesn't match GameBanana's). Try again.")
            }
        }
        target.delete()
        if (!partial.renameTo(target)) {
            partial.copyTo(target, overwrite = true)
            partial.delete()
        }
        progress(1f)
        return target
    }
}
