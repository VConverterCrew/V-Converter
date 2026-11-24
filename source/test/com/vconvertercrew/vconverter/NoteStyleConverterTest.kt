package com.vconvertercrew.vconverter

import com.vconvertercrew.vconverter.core.RgbaImage
import com.vconvertercrew.vconverter.editor.SpriteAtlas
import com.vconvertercrew.vconverter.fpsplus.FpsIcon
import com.vconvertercrew.vconverter.io.OutputFile
import com.vconvertercrew.vconverter.json.JSONObject
import com.vconvertercrew.vconverter.notestyle.Base64Text
import com.vconvertercrew.vconverter.notestyle.NoteStyleConverter
import com.vconvertercrew.vconverter.notestyle.NoteStyleInput
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NoteStyleConverterTest {
    private val colors = listOf("purple", "blue", "green", "red")
    private val dirs = listOf("LEFT", "DOWN", "UP", "RIGHT")

    private fun fill(image: RgbaImage, x: Int, y: Int, w: Int, h: Int, r: Int, g: Int, b: Int) {
        for (py in y until y + h) for (px in x until x + w) {
            val o = (py * image.width + px) * 4
            image.pixels[o] = r.toByte()
            image.pixels[o + 1] = g.toByte()
            image.pixels[o + 2] = b.toByte()
            image.pixels[o + 3] = 255.toByte()
        }
    }

    private fun psychSheet(template: Boolean): Pair<RgbaImage, String> {
        val image = RgbaImage.blank(400, 200)
        val xml = StringBuilder("<TextureAtlas imagePath=\"x.png\">\n")
        colors.forEachIndexed { i, c ->
            val x = if (template) 0 else i * 40
            if (template) fill(image, 0, 0, 40, 40, 255, 0, 0) else fill(image, x, 0, 40, 40, 50 + i * 60, 200 - i * 40, i * 50)
            xml.append("<SubTexture name=\"${c}0000\" x=\"$x\" y=\"0\" width=\"40\" height=\"40\"/>\n")
            xml.append("<SubTexture name=\"${c} hold piece0000\" x=\"${200 + i * 20}\" y=\"0\" width=\"10\" height=\"8\"/>\n")
            xml.append("<SubTexture name=\"${c} hold end0000\" x=\"${200 + i * 20}\" y=\"10\" width=\"10\" height=\"18\"/>\n")
            fill(image, 200 + i * 20, 0, 10, 28, 255, 0, 0)
            xml.append("<SubTexture name=\"arrow${dirs[i]}0000\" x=\"${i * 40}\" y=\"50\" width=\"40\" height=\"40\"/>\n")
            xml.append("<SubTexture name=\"${dirs[i].lowercase()} press0000\" x=\"${i * 40}\" y=\"100\" width=\"40\" height=\"40\"/>\n")
            xml.append("<SubTexture name=\"${dirs[i].lowercase()} confirm0000\" x=\"${i * 40}\" y=\"150\" width=\"40\" height=\"40\" frameX=\"-5\" frameY=\"-5\" frameWidth=\"50\" frameHeight=\"50\"/>\n")
            fill(image, i * 40, 50, 40, 140, 128, 128, 128)
        }
        xml.append("</TextureAtlas>\n")
        return image to xml.toString()
    }

    private fun inputs(template: Boolean, prefix: String = "images/noteSkins/NOTE_assets-test"): List<NoteStyleInput> {
        val (image, xml) = psychSheet(template)
        return listOf(NoteStyleInput("$prefix.png", null, image), NoteStyleInput("$prefix.xml", xml, null))
    }

    private fun files(outputs: List<OutputFile>) = outputs.associateBy { it.relativePath }

    @Test
    fun psychTemplateBecomesColoredVSliceStyle() {
        val out = files(NoteStyleConverter("psych", "vslice").convert(inputs(template = true)).files)
        val json = JSONObject(out.getValue("data/notestyles/psych-test.json").content)
        val assets = json.getJSONObject("assets")
        assertEquals("notestyles/psych-test/notes", assets.getJSONObject("note").getString("assetPath"))
        assertEquals("noteLeft", assets.getJSONObject("note").getJSONObject("data").getJSONObject("left").getString("prefix"))
        assertTrue(assets.getJSONObject("noteStrumline").getJSONObject("data").has("rightConfirmHold"))
        assertEquals("notestyles/psych-test/NOTE_hold_assets", assets.getJSONObject("holdNote").getString("assetPath"))
        val strip = FpsIcon.decode(out.getValue("images/notestyles/psych-test/NOTE_hold_assets.png").bytes!!)!!
        assertEquals(80, strip.width)
        assertEquals(20, strip.height)
        val sheet = FpsIcon.decode(out.getValue("images/notestyles/psych-test/notes.png").bytes!!)!!
        val atlas = SpriteAtlas.parse(out.getValue("images/notestyles/psych-test/notes.xml").content)
        val left = atlas.framesFor("noteLeft").first()
        val right = atlas.framesFor("noteRight").first()
        assertTrue(left.x != right.x || left.y != right.y)
        assertEquals(0xFFC24B99.toInt(), sheet.argb(left.x + 5, left.y + 5))
        assertEquals(0xFFF9393F.toInt(), sheet.argb(right.x + 5, right.y + 5))
        val confirm = atlas.framesFor("confirmLeft").first()
        assertEquals(-5, confirm.frameX)
        assertEquals(50, confirm.frameWidth)
    }

    @Test
    fun coloredSheetGoesToEveryEngine() {
        val source = inputs(template = false)
        val psych = files(NoteStyleConverter("vslice", "psych").convert(files(NoteStyleConverter("psych", "vslice").convert(source).files).values.map { f ->
            if (f.bytes != null) NoteStyleInput(f.relativePath, null, FpsIcon.decode(f.bytes!!)) else NoteStyleInput(f.relativePath, f.content, null)
        }).files)
        val xml = psych.getValue("images/noteSkins/NOTE_assets-psych-test.xml").content
        assertTrue(xml.contains("name=\"purple0000\""))
        assertTrue(xml.contains("name=\"arrowLEFT0000\""))
        assertTrue(xml.contains("name=\"red hold end0000\""))
        assertFalse(psych.containsKey("images/noteSkins/list.txt"))

        val codename = files(NoteStyleConverter("psych", "codename").convert(source).files)
        assertTrue(codename.getValue("notestyle-scripts/psych-test.hx").content.contains("event.noteSprite = \"game/notes/psych-test\";"))
        assertNotNull(codename["images/game/notes/psych-test.png"])

        val nmv = files(NoteStyleConverter("psych", "nmv").convert(source).files)
        val skin = JSONObject(nmv.getValue("data/noteskins/psych-test.json").content)
        assertEquals("noteSkins/psych-test", skin.getString("noteTexture"))
        assertFalse(skin.getBoolean("inGameColoring"))

        val fps = files(NoteStyleConverter("psych", "fpsplus").convert(source).files)
        assertTrue(fps.getValue("data/uiSkins/note/PsychTest.hxc").content.contains("class PsychTest extends NoteSkinBase"))
        assertEquals("PsychTest", JSONObject(fps.getValue("data/uiSkins/PsychTest.json").content).getString("playerNotes"))
    }

    @Test
    fun templateStaysTemplateForPsychAndNmv() {
        val nmv = files(NoteStyleConverter("psych", "nmv").convert(inputs(template = true)).files)
        val skin = JSONObject(nmv.getValue("data/noteskins/psych-test.json").content)
        assertTrue(skin.getBoolean("inGameColoring"))
        assertEquals(4, skin.getJSONArray("arrowRGB").length())
        val back = files(NoteStyleConverter("nmv", "psych").convert(nmv.values.map { f ->
            if (f.bytes != null) NoteStyleInput(f.relativePath, null, FpsIcon.decode(f.bytes!!)) else NoteStyleInput(f.relativePath, f.content, null)
        }).files)
        assertEquals("nmv-psych-test", back.getValue("images/noteSkins/list.txt").content.trim())
    }

    @Test
    fun base64RoundTrip() {
        val bytes = ByteArray(1000) { (it * 7).toByte() }
        assertTrue(Base64Text.decode(Base64Text.encode(bytes)).contentEquals(bytes))
        assertEquals("TWFu", Base64Text.encode("Man".encodeToByteArray()))
        assertEquals("TWE=", Base64Text.encode("Ma".encodeToByteArray()))
    }
}
