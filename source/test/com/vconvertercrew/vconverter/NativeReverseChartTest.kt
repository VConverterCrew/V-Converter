package com.vconvertercrew.vconverter

import com.vconvertercrew.vconverter.convert.reverse.VSliceParser
import com.vconvertercrew.vconverter.convert.reverse.VSliceSongSource
import com.vconvertercrew.vconverter.convert.reverse.VSliceToPsychChart
import com.vconvertercrew.vconverter.convert.script.VScriptNative
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test

class NativeReverseChartTest {
    private fun metadata(name: String, style: String, difficulties: String) = """
        {"version":"2.2.4","songName":"$name","artist":"Kawai Sprite","charter":"x","timeChanges":[{"t":0,"b":0,"bpm":150,"n":4,"d":4},{"t":12800,"b":32,"bpm":180.5,"n":3,"d":4}],
         "offsets":{"instrumental":-12.25},
         "playData":{"difficulties":[$difficulties],"characters":{"player":"bf","girlfriend":"","opponent":"pico"},"stage":"mainStage","noteStyle":"$style","songVariations":["erect"]}}
    """.trimIndent()

    private fun chart(): String {
        val notes = StringBuilder()
        for (i in 0 until 300) {
            if (i > 0) notes.append(',')
            val kind = when (i % 7) { 0 -> ",\"k\":\"alt\""; 3 -> ",\"k\":\"hurt\""; 5 -> ",\"k\":\"myKind\""; else -> "" }
            notes.append("{\"t\":${i * 133.333},\"d\":${i % 9},\"l\":${if (i % 4 == 0) 250.5 else 0}$kind}")
        }
        val events = """
            {"t":0,"e":"FocusCamera","v":{"char":0}},
            {"t":1600,"e":"FocusCamera","v":{"char":1,"x":10,"y":-5}},
            {"t":2000,"e":"FocusCamera","v":{"char":-1,"x":12.5,"y":-7}},
            {"t":3200,"e":"FocusCamera","v":2},
            {"t":3300,"e":"PlayAnimation","v":{"target":"girlfriend","anim":"cheer","force":true}},
            {"t":3300,"e":"PlayAnimation","v":{"target":"dad","anim":"singLEFT"}},
            {"t":4000,"e":"ScrollSpeed","v":{"scroll":1.3,"duration":8,"ease":"linear","absolute":false}},
            {"t":4100,"e":"ScrollSpeed","v":{"scroll":3,"ease":"INSTANT","absolute":true}},
            {"t":5000,"e":"ZoomCamera","v":{"zoom":1.2,"duration":4,"mode":"direct"}},
            {"t":5100,"e":"ZoomCamera","v":{"zoom":1.1,"duration":4,"mode":"stage"}},
            {"t":6000,"e":"SetCameraBop","v":{"rate":2,"intensity":1.75}},
            {"t":7000,"e":"PsychEvent","v":{"name":"Change Character","value1":"bf","value2":"bf-car"}},
            {"t":8000,"e":"MyEvent","v":{"value1":"a","value2":3}},
            {"t":8000,"e":"OtherEvent","v":{"foo":1.5,"bar":[1,2]}},
            {"t":9000,"e":"Plain","v":42}
        """.trimIndent()
        return """{"version":"2.0.0","scrollSpeed":{"easy":1.8,"normal":2.1,"hard":2.6},"events":[$events],"notes":{"easy":[$notes],"normal":[$notes],"hard":[$notes]},"generatedBy":"test"}"""
    }

    @Test
    fun nativeMatchesKotlin() {
        if (!VScriptNative.available) return
        val meta = metadata("Test Song!", "pixel", "\"easy\",\"normal\",\"hard\"")
        val erect = metadata("Test Song!", "funkin", "\"erect\",\"nightmare\"")
        val chartText = chart()
        val erectChart = chartText.replace("\"easy\"", "\"erect\"").replace("\"normal\"", "\"nightmare\"")
        val sources = listOf(
            VSliceSongSource("", VSliceParser.metadata(meta), VSliceParser.chart(chartText)),
            VSliceSongSource("erect", VSliceParser.metadata(erect), VSliceParser.chart(erectChart))
        )
        val kotlin = VSliceToPsychChart.convertAll("test-song", sources, 0.9)
        val native = VSliceToPsychChart.convertTexts("test-song", listOf(
            Triple("metadata", "", meta), Triple("chart", "", chartText), Triple("metadata", "erect", erect), Triple("chart", "erect", erectChart)
        ), 0.9, single = false)
        assertNotNull(native)
        assertEquals(kotlin.size, native!!.size)
        kotlin.zip(native).forEach { (k, n) ->
            assertEquals(k.songId, n.songId)
            assertEquals(k.songName, n.songName)
            assertEquals(k.difficulties, n.difficulties)
            assertEquals(k.files.map { it.path }, n.files.map { it.path })
            k.files.zip(n.files).forEach { (a, b) -> assertEquals(a.path, a.json, b.json) }
            assertEquals(k.report.warnings, n.report.warnings)
            assertEquals(k.report.sections.map { it.title to it.lines }, n.report.sections.map { it.title to it.lines })
            assertEquals(k.report.stats.map { it.label to it.value.toString() }, n.report.stats.map { it.label to it.value.toString() })
        }
    }
}
