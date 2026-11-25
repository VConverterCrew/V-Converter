package com.vconvertercrew.vconverter.tools.online

interface WebSocketListener {
    fun onOpen() {}
    fun onText(text: String)
    fun onClose(code: Int, reason: String) {}
    fun onError(error: Throwable) {}
}
