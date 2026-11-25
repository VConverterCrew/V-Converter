package com.vconvertercrew.vconverter.views.overlay

import platform.Foundation.NSProcessInfo

object MemoryStats {
    fun usedBytes(): Long? = null

    fun limitBytes(): Long? = NSProcessInfo.processInfo.physicalMemory.toLong().takeIf { it > 0 }
}
