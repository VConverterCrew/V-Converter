package com.vconvertercrew.vconverter.views.editor

interface SongPlayer {
    val duration: Double
    val isPlaying: Boolean
    fun position(): Double
    fun play()
    fun pause()
    fun seek(ms: Double)
    fun hitsound()
    fun release()
}
