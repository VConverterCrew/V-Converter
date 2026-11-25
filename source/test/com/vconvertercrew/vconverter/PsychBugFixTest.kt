package com.vconvertercrew.vconverter

import com.vconvertercrew.vconverter.convert.character.CharacterSource
import com.vconvertercrew.vconverter.convert.character.PsychCharacterParser
import com.vconvertercrew.vconverter.convert.character.PsychToVSliceCharacter
import com.vconvertercrew.vconverter.engines.codename.CodenameCharacterParser
import com.vconvertercrew.vconverter.core.CharacterSlot
import com.vconvertercrew.vconverter.core.SparrowAtlas
import com.vconvertercrew.vconverter.json.JSONObject
import com.vconvertercrew.vconverter.convert.reverse.VSliceParser
import com.vconvertercrew.vconverter.convert.reverse.VSliceToPsychCharacter
import com.vconvertercrew.vconverter.convert.reverse.VSliceToPsychStage
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PsychBugFixTest {
    private val packerText = """
        ghost idle_0 = 0 0 128 128
        ghost idle_1 = 129 0 128 128
        ghost left_0 = 0 129 140 128
    """.trimIndent()

    @Test
    fun packerSheetsParse() {
        assertTrue(SparrowAtlas.isPacker(packerText))
        assertFalse(SparrowAtlas.isPacker("just some notes\nnothing here"))
        val atlas = SparrowAtlas.parseAny(packerText)
        assertTrue(atlas.packer)
        assertEquals(3, atlas.frames.size)
        assertEquals("ghost left_0", atlas.frames[2].name)
        assertEquals(140.0, atlas.frames[2].sourceWidth, 0.0)
        assertFalse(SparrowAtlas.parseAny("<TextureAtlas><SubTexture name=\"a0000\" x=\"0\" y=\"0\" width=\"5\" height=\"5\"/></TextureAtlas>").packer)
    }

    @Test
    fun packerCharacterKeepsPackerRender() {
        val psych = PsychCharacterParser.parse("""{"image":"characters/ghost","scale":6,"no_antialiasing":true,"position":[0,0],"animations":[{"anim":"idle","name":"ghost idle_","fps":24,"offsets":[0,0]},{"anim":"singLEFT","name":"ghost left_","fps":24,"offsets":[-3,0]}]}""")
        val result = PsychToVSliceCharacter.convert(CharacterSource("ghost", psych, CharacterSlot.OPPONENT, mapOf("ghost" to SparrowAtlas.parsePacker(packerText))))
        val json = JSONObject(result.json)
        assertEquals("packer", json.getString("renderType"))
        assertEquals("Exact", result.report.stats.first { it.label == "Accuracy" }.value.toString())
        assertTrue(result.report.warnings.none { "approximate" in it })
        val back = VSliceToPsychCharacter.convert("ghost", VSliceParser.character(result.json), CharacterSlot.OPPONENT, mapOf("ghost" to SparrowAtlas.parsePacker(packerText)))
        assertTrue(back.report.warnings.none { "not a Sparrow sheet" in it || "approximate" in it })
    }

    @Test
    fun alternatingCharactersUseDanceRightHitbox() {
        val atlas = SparrowAtlas.parse("""<TextureAtlas><SubTexture name="left0000" x="0" y="0" width="200" height="400"/><SubTexture name="right0000" x="0" y="0" width="300" height="400"/></TextureAtlas>""")
        val psych = PsychCharacterParser.parse("""{"image":"characters/dancer","scale":1,"position":[0,0],"animations":[{"anim":"danceLeft","name":"left","fps":24,"offsets":[0,0]},{"anim":"danceRight","name":"right","fps":24,"offsets":[0,0]}]}""")
        val result = PsychToVSliceCharacter.convert(CharacterSource("dancer", psych, CharacterSlot.OPPONENT, mapOf("dancer" to atlas)))
        val offsets = JSONObject(result.json).getJSONArray("animations").getJSONObject(0).getJSONArray("offsets")
        assertEquals(85.0, offsets.getDouble(0), 1e-6)
        assertEquals(385.0, offsets.getDouble(1), 1e-6)
        val back = VSliceToPsychCharacter.convert("dancer", VSliceParser.character(result.json), CharacterSlot.OPPONENT, mapOf("dancer" to atlas))
        val psychBack = JSONObject(back.json)
        val position = psychBack.getJSONArray("position")
        val anim = psychBack.getJSONArray("animations").getJSONObject(0).getJSONArray("offsets")
        assertEquals(0.0, position.getDouble(0) - anim.getDouble(0), 1e-6)
        assertEquals(0.0, position.getDouble(1) - anim.getDouble(1), 1e-6)
    }

    @Test
    fun pixelStagesStayPixel() {
        val stage = VSliceParser.stage("""{"version":"1.0.0","name":"School","cameraZoom":1.05,"props":[],"characters":{"bf":{"position":[970,320]},"dad":{"position":[100,100]},"gf":{"position":[580,430]}}}""")
        val school = JSONObject(VSliceToPsychStage.convert("school", stage).json)
        assertTrue(school.getBoolean("isPixelStage"))
        assertEquals("pixel", school.getString("stageUI"))
        val evil = JSONObject(VSliceToPsychStage.convert("schoolEvilErect", stage).json)
        assertEquals("pixel", evil.getString("stageUI"))
        val main = JSONObject(VSliceToPsychStage.convert("mainStage", stage).json)
        assertEquals("normal", main.getString("stageUI"))
    }

    @Test
    fun codenameKeepsFrozenAnimations() {
        val character = CodenameCharacterParser.parse("frozen", """<character><anim name="idle" anim="idle0" fps="0" loop="false"/></character>""")
        assertEquals(0.0, character.animations.first().fps, 0.0)
    }
}
