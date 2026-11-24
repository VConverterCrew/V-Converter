package com.vconvertercrew.vconverter.online

import kotlinx.cinterop.ByteVar
import kotlinx.cinterop.CPointer
import kotlinx.cinterop.toKString
import vbanana.vbanana_free
import vbanana.vbanana_inspect
import vbanana.vbanana_link
import vbanana.vbanana_md5_file
import vbanana.vbanana_parse
import vbanana.vbanana_request

object VBananaNative {
    val available: Boolean = true

    private fun take(result: CPointer<ByteVar>?): String? {
        if (result == null) return null
        return try {
            result.toKString()
        } finally {
            vbanana_free(result)
        }
    }

    fun request(json: String): String? = take(vbanana_request(json))

    fun parse(kind: String, body: String, options: String = "{}"): String? = take(vbanana_parse(kind, body, options))

    fun link(text: String): String? = take(vbanana_link(text))

    fun inspect(entries: String): String? = take(vbanana_inspect(entries))

    fun md5File(path: String): String? = take(vbanana_md5_file(path))
}
