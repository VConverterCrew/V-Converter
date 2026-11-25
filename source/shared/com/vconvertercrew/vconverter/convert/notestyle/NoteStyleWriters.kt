package com.vconvertercrew.vconverter.convert.notestyle

import com.vconvertercrew.vconverter.core.PngWriter
import com.vconvertercrew.vconverter.core.RgbaImage
import com.vconvertercrew.vconverter.json.JSONArray
import com.vconvertercrew.vconverter.json.JSONObject
import kotlin.math.roundToInt

object NoteStyleWriters {
    const val VSLICE_VERSION = "1.1.0"

    fun write(engine: String, style: NoteStyle): NoteStyleResult = when (engine) {
        "psych" -> Psych(style).run()
        "codename" -> Codename(style).run()
        "nmv" -> Nmv(style).run()
        "fpsplus" -> Fps(style).run()
        else -> VSlice(style).run()
    }

    private fun round(value: Double): Double = (value * 100.0).roundToInt() / 100.0

    private fun splashCenter(anim: Anim): Triple<Double, Double, Pair<Double, Double>> {
        val f = anim.first
        return Triple(f.frameWidth.toDouble(), f.frameHeight.toDouble(), NoteImages.center(anim.sheet, f))
    }

    private abstract class Base(val style: NoteStyle) {
        val files = ArrayList<NoteStyleOutput>()
        val lines = ArrayList<String>()
        val warnings = ArrayList<String>()

        abstract fun run(): NoteStyleResult

        fun png(path: String, image: RgbaImage) {
            files += NoteStyleOutput(path, null, PngWriter.encode(image))
        }

        fun text(path: String, text: String) {
            files += NoteStyleOutput(path, text, null)
        }

        fun sheet(builder: SheetBuilder, imagePath: String) {
            val (image, xml) = builder.build(imagePath.substringAfterLast('/') + ".png")
            png("images/$imagePath.png", image)
            text("images/$imagePath.xml", xml)
            if (image.width > 4096 || image.height > 4096) warnings += "images/$imagePath.png is ${image.width}×${image.height}; some phones can't load textures over 4096 pixels."
        }

        fun bake(dir: NoteDir, target: Boolean): Rgb? = if (target) style.rgb?.get(dir.ordinal) else null

        fun splashBake(dir: NoteDir, target: Boolean): Rgb? = if (target) style.splashRgb?.get(dir.ordinal) else null

        fun result(title: String): NoteStyleResult = NoteStyleResult(files, listOf(title to (style.info + lines)), warnings)

        fun psychSheet(path: String, bakeColors: Boolean, fixedScale: Double?) {
            val smooth = !style.pixel
            val builder = SheetBuilder(1.0, smooth)
            val noteFactor = fixedScale?.let { style.noteScale / it } ?: 1.0
            val strumFactor = fixedScale?.let { style.strumScale / it } ?: 1.0
            val holdFactor = fixedScale?.let { style.holdScale / it } ?: 1.0
            NoteDir.entries.forEach { dir ->
                style.notes[dir]?.let { builder.add(dir.color, it, bake(dir, bakeColors), noteFactor) }
                style.holds[dir]?.let {
                    builder.add("${dir.color} hold piece", it.piece, bake(dir, bakeColors), holdFactor)
                    builder.add("${dir.color} hold end", it.end, bake(dir, bakeColors), holdFactor)
                }
                style.strums[dir]?.let { s ->
                    builder.add("arrow${dir.upper}", s.static, null, strumFactor)
                    builder.add("${dir.key} press", s.press ?: s.static, if (s.press != null) bake(dir, bakeColors) else null, strumFactor)
                    builder.add("${dir.key} confirm", s.confirm ?: s.press ?: s.static, if (s.confirm != null || s.press != null) bake(dir, bakeColors) else null, strumFactor)
                }
            }
            if (fixedScale != null && listOf(noteFactor, strumFactor, holdFactor).any { it != 1.0 }) {
                lines += "The sprites were resized by ${round(noteFactor)}× because the target always draws notes at ${fixedScale}× (the source used ${round(style.noteScale)}×)."
            }
            if (!builder.isEmpty) sheet(builder, path)
        }
    }

    private class VSlice(style: NoteStyle) : Base(style) {
        override fun run(): NoteStyleResult {
            val dir = "notestyles/${style.id}"
            val assets = JSONObject()
            val smooth = !style.pixel
            if (style.complete || style.hasStrums) {
                val builder = SheetBuilder(1.0, smooth)
                NoteDir.entries.forEach { d ->
                    style.notes[d]?.let { builder.add("note${d.pascal}", it, bake(d, true)) }
                    style.strums[d]?.let { s ->
                        builder.add("static${d.pascal}", s.static)
                        builder.add("press${d.pascal}", s.press ?: s.static, if (s.press != null) bake(d, true) else null)
                        builder.add("confirm${d.pascal}", s.confirm ?: s.press ?: s.static, if (s.confirm != null || s.press != null) bake(d, true) else null)
                    }
                }
                sheet(builder, "$dir/notes")
            }
            if (style.complete) {
                val data = JSONObject()
                NoteDir.entries.forEach { d -> data.put(d.key, animData("note${d.pascal}", style.notes.getValue(d))) }
                assets.put("note", JSONObject().put("assetPath", "$dir/notes").put("scale", round(style.noteScale)).put("isPixel", style.pixel).put("data", data))
            }
            if (style.hasStrums) {
                val data = JSONObject()
                NoteDir.entries.forEach { d ->
                    val s = style.strums.getValue(d)
                    data.put("${d.key}Static", animData("static${d.pascal}", s.static))
                    data.put("${d.key}Press", animData("press${d.pascal}", s.press ?: s.static))
                    data.put("${d.key}Confirm", animData("confirm${d.pascal}", s.confirm ?: s.press ?: s.static))
                    data.put("${d.key}ConfirmHold", animData("confirm${d.pascal}", s.confirm ?: s.press ?: s.static))
                }
                assets.put("noteStrumline", JSONObject().put("assetPath", "$dir/notes").put("scale", round(style.strumScale)).put("offsets", JSONArray(listOf(0, 0))).put("isPixel", style.pixel).put("data", data))
            }
            if (style.hasHolds) {
                png("images/$dir/NOTE_hold_assets.png", HoldStrip.build(style.holds, style.pixel, style.rgb))
                assets.put("holdNote", JSONObject().put("assetPath", "$dir/NOTE_hold_assets").put("scale", round(style.holdScale)).put("isPixel", style.pixel).put("data", JSONObject()))
                lines += "Hold notes were rebuilt as a V-Slice hold strip (images/$dir/NOTE_hold_assets.png) from the sheet's hold pieces and ends."
            }
            if (style.hasSplashes) {
                val builder = SheetBuilder(1.0, smooth)
                val data = JSONObject().put("enabled", true)
                val all = NoteDir.entries.flatMap { style.splashes[it].orEmpty() }
                val fps = all.map { it.fps }.average()
                val variance = all.map { it.variance }.average()
                data.put("framerateDefault", fps.roundToInt()).put("framerateVariance", variance.roundToInt())
                NoteDir.entries.forEach { d ->
                    val list = JSONArray()
                    style.splashes[d].orEmpty().forEachIndexed { i, anim ->
                        val name = "splash${d.pascal}${i + 1}"
                        builder.add(name, anim, splashBake(d, true))
                        list.put(JSONObject().put("prefix", name))
                    }
                    data.put("${d.key}Splashes", list)
                }
                sheet(builder, "$dir/noteSplashes")
                val first = all.first()
                val s = style.splashScale
                val (fw, fh, c) = splashCenter(first)
                val ox = (52.0 + 0.3 * fw * s - fw / 2 - (c.first - fw / 2) * s) / s
                val oy = (52.0 + 0.3 * fh * s - fh / 2 - (c.second - fh / 2) * s) / s
                assets.put("noteSplash", JSONObject().put("assetPath", "$dir/noteSplashes").put("scale", round(s)).put("alpha", round(style.splashAlpha))
                    .put("isPixel", style.pixel).put("offsets", JSONArray(listOf(round(ox), round(oy)))).put("data", data))
                lines += "Splash offsets were worked out from where the splash art sits in its frames, so it lands centered on the strum."
            }
            val root = JSONObject().put("version", VSLICE_VERSION).put("name", style.name).put("author", style.author).put("fallback", "funkin").put("assets", assets)
            text("data/notestyles/${style.id}.json", root.toString(2))
            if (style.rgb != null || style.splashRgb != null) lines += "V-Slice has no note color shader, so the colors were painted into the images."
            lines += "Use it in a song with \"noteStyle\": \"${style.id}\" in playData of the song's -metadata.json."
            return result("Note style ${style.id}")
        }

        private fun animData(prefix: String, anim: Anim): JSONObject {
            val o = JSONObject().put("prefix", prefix)
            if (anim.fps != 24.0) o.put("frameRate", anim.fps.roundToInt())
            if (anim.looped) o.put("looped", true)
            return o
        }
    }

    private class Psych(style: NoteStyle) : Base(style) {
        override fun run(): NoteStyleResult {
            val file = "NOTE_assets-${style.id}"
            val keepRgb = style.rgb != null
            if (style.complete || style.hasStrums || style.hasHolds) {
                psychSheet("noteSkins/$file", bakeColors = false, fixedScale = 0.7)
                lines += "Use it in a song with \"arrowSkin\": \"noteSkins/$file\" in the chart (Psych's chart editor → Song → Note Texture)."
                if (keepRgb) {
                    text("images/noteSkins/list.txt", style.id + "\n")
                    lines += "It is an RGB note skin like Psych's own, so it was also added to images/noteSkins/list.txt and shows up in Options → Visuals → Note Skins."
                } else {
                    lines += "The notes are already colored: turn on \"disableNoteRGB\" in the chart (Song → Disable Note RGB), or Psych tints them with the note colors."
                }
                if (!style.hasHolds) lines += "There were no hold notes to convert, so holds come from the same sheet's defaults and may be missing."
            }
            if (style.hasSplashes) {
                val splash = "noteSplashes-${style.id}"
                val builder = SheetBuilder(1.0, !style.pixel)
                val animations = JSONObject()
                NoteDir.entries.forEach { d ->
                    style.splashes[d].orEmpty().forEachIndexed { i, anim ->
                        val prefix = "note splash ${d.color} ${i + 1}"
                        builder.add(prefix, anim, null)
                        val s = style.splashScale
                        val (fw, fh, c) = splashCenter(anim)
                        val ox = fw / 2 + (c.first - fw / 2) * s - 172.4
                        val oy = fh / 2 + (c.second - fh / 2) * s - 178.0
                        val key = if (i == 0) d.color else d.color + (i + 1)
                        animations.put(key, JSONObject()
                            .put("prefix", prefix)
                            .put("fps", JSONArray(listOf((anim.fps - anim.variance).roundToInt(), (anim.fps + anim.variance).roundToInt())))
                            .put("offsets", JSONArray(listOf(round(ox), round(oy))))
                            .put("indices", JSONArray())
                            .put("name", key)
                            .put("noteData", d.ordinal + i * 4))
                    }
                }
                sheet(builder, "noteSplashes/$splash")
                val config = JSONObject().put("animations", animations).put("allowRGB", style.splashRgb != null).put("allowPixel", false).put("scale", round(style.splashScale))
                text("images/noteSplashes/$splash.json", config.toString(2))
                if (style.splashRgb != null) text("images/noteSplashes/list.txt", style.id + "\n")
                lines += "Splashes: \"splashSkin\": \"noteSplashes/$splash\" in the chart (Note Splashes Texture)."
            }
            return result("Psych note skin ${style.id}")
        }
    }

    private class Codename(style: NoteStyle) : Base(style) {
        override fun run(): NoteStyleResult {
            val path = "game/notes/${style.id}"
            val hasNotes = style.complete || style.hasStrums || style.hasHolds
            if (hasNotes) psychSheet(path, bakeColors = true, fixedScale = 0.7)
            if (style.hasSplashes) {
                val builder = SheetBuilder(1.0, !style.pixel)
                val xml = StringBuilder()
                xml.append("<!DOCTYPE codename-engine-splashes>\n<splashes sprite=\"game/splashes/${style.id}\" alpha=\"${round(style.splashAlpha)}\" scale=\"${round(style.splashScale)}\"")
                if (style.pixel) xml.append(" antialiasing=\"false\"")
                xml.append(">\n")
                NoteDir.entries.forEach { d ->
                    xml.append("\t<strum id=\"${d.ordinal}\">\n")
                    style.splashes[d].orEmpty().forEachIndexed { i, anim ->
                        val prefix = "splash ${d.key} ${i + 1}"
                        builder.add(prefix, anim, splashBake(d, true))
                        val (fw, fh, c) = splashCenter(anim)
                        val ox = (c.first - fw / 2) * style.splashScale
                        val oy = (c.second - fh / 2) * style.splashScale
                        xml.append("\t\t<anim name=\"splash${d.key}${i + 1}\" anim=\"$prefix\" fps=\"${anim.fps.roundToInt()}\" x=\"${round(ox)}\" y=\"${round(oy)}\" />\n")
                    }
                    xml.append("\t</strum>\n")
                }
                xml.append("</splashes>\n")
                sheet(builder, "game/splashes/${style.id}")
                text("data/splashes/${style.id}.xml", xml.toString())
            }
            val script = StringBuilder()
            script.append("function onNoteCreation(event)\n{\n\tif (event.note.noteType == null || event.note.noteType == \"\")\n\t{\n")
            if (hasNotes) script.append("\t\tevent.noteSprite = \"$path\";\n")
            if (style.hasSplashes) script.append("\t\tevent.note.splash = \"${style.id}\";\n")
            script.append("\t}\n}\n")
            if (style.hasStrums) script.append("\nfunction onStrumCreation(event)\n{\n\tevent.sprite = \"$path\";\n}\n")
            text("notestyle-scripts/${style.id}.hx", script.toString())
            if (style.rgb != null || style.splashRgb != null) lines += "Codename has no note color shader, so the colors were painted into the images."
            lines += "Copy notestyle-scripts/${style.id}.hx into songs/<song>/scripts/ for each song that should use it (or data/scripts/ for every song)."
            return result("Codename note skin ${style.id}")
        }
    }

    private class Nmv(style: NoteStyle) : Base(style) {
        override fun run(): NoteStyleResult {
            val texture = "noteSkins/${style.id}"
            val json = JSONObject()
            val hasNotes = style.complete || style.hasStrums || style.hasHolds
            if (hasNotes) {
                psychSheet(texture, bakeColors = false, fixedScale = null)
                json.put("noteTexture", texture)
                val notes = JSONArray()
                val receptors = JSONArray()
                NoteDir.entries.forEach { d ->
                    notes.put(JSONArray(listOf(
                        anim("scroll", "${d.color}0", style.notes[d]),
                        anim("hold", "${d.color} hold piece", style.holds[d]?.piece),
                        anim("holdend", "${d.color} hold end", style.holds[d]?.end)
                    )))
                    receptors.put(JSONArray(listOf(
                        anim("static", "arrow${d.upper}", style.strums[d]?.static),
                        anim("pressed", "${d.key} press", style.strums[d]?.press),
                        anim("confirm", "${d.key} confirm", style.strums[d]?.confirm)
                    )))
                }
                json.put("noteAnimations", notes).put("receptorAnimations", receptors)
            }
            if (style.hasSplashes) {
                val builder = SheetBuilder(1.0, !style.pixel)
                val splashes = JSONArray()
                NoteDir.entries.forEach { d ->
                    val anim = style.splashes[d].orEmpty().firstOrNull()
                    if (anim == null) {
                        splashes.put(JSONObject().put("anim", "note${d.ordinal}").put("xmlName", "note splash ${d.color}"))
                        return@forEach
                    }
                    val prefix = "note splash ${d.color} 1"
                    builder.add(prefix, anim, null)
                    val (fw, fh, c) = splashCenter(anim)
                    splashes.put(JSONObject().put("anim", "note${d.ordinal}").put("xmlName", prefix).put("fps", anim.fps.roundToInt())
                        .put("offsets", JSONArray(listOf(round((c.first - fw / 2) * style.splashScale), round((c.second - fh / 2) * style.splashScale)))))
                }
                sheet(builder, "noteSplashes/${style.id}")
                json.put("splashTexture", "noteSplashes/${style.id}").put("noteSplashAnimations", splashes).put("splashesEnabled", true)
                if (style.splashes.values.any { it.size > 1 }) lines += "NightmareVision plays one splash per direction, so only the first splash of each direction was kept."
            }
            json.put("noteScale", round(style.noteScale)).put("receptorScale", round(style.strumScale)).put("splashScale", round(style.splashScale)).put("splashAlpha", round(style.splashAlpha))
                .put("antialiasing", !style.pixel).put("inGameColoring", style.rgb != null)
            style.rgb?.let { colors ->
                json.put("arrowRGB", JSONArray(colors.map { c -> JSONObject().put("r", unsigned(c.r)).put("g", unsigned(c.g)).put("b", unsigned(c.b)) }))
                lines += "The notes are an RGB template, so inGameColoring stays on with the same colors."
            }
            text("data/noteskins/${style.id}.json", json.toString(2))
            lines += "Use it in a song with \"arrowSkins\": [\"${style.id}\", \"${style.id}\"] in the chart (one entry per lane)."
            return result("NightmareVision noteskin ${style.id}")
        }

        private fun unsigned(color: Int): Long = color.toLong() and 0xFFFFFFFFL

        private fun anim(name: String, prefix: String, anim: Anim?): JSONObject {
            val o = JSONObject().put("anim", name).put("xmlName", prefix).put("offsets", JSONArray(listOf(0, 0))).put("looping", name == "hold" || name == "holdend")
            if (anim != null && anim.fps != 24.0) o.put("fps", anim.fps.roundToInt())
            return o
        }
    }

    private class Fps(style: NoteStyle) : Base(style) {
        override fun run(): NoteStyleResult {
            val cls = style.id.split('-', '_').filter { it.isNotEmpty() }.joinToString("") { it.replaceFirstChar { c -> c.uppercaseChar() } }.let { if (it.firstOrNull()?.isDigit() != false) "Skin$it" else it }
            val path = "ui/notes/${style.id}"
            val hasNotes = style.complete || style.hasStrums || style.hasHolds
            if (hasNotes) psychSheet(path, bakeColors = true, fixedScale = null)
            val skin = JSONObject().put("comboPopup", "Default").put("countdown", "Default")
            if (style.complete) {
                val hxc = StringBuilder()
                hxc.append("package noteskins;\n\nclass $cls extends NoteSkinBase\n{\n\n\tpublic function new(){\n\t\tsuper();\n\t\t\n")
                hxc.append("\t\tinfo.path = \"$path\";\n\t\tinfo.frameLoadType = setSparrow();\n\t\tinfo.scale = ${round(style.noteScale)};\n\t\tinfo.canGlow = false;\n")
                if (style.pixel) hxc.append("\t\tinfo.antialiasing = false;\n")
                NoteDir.entries.forEach { d ->
                    hxc.append("\t\t\n\t\tsetScrollAnimPrefix(${d.key}, \"${d.color}0\");\n")
                    if (style.holds[d] != null) {
                        hxc.append("\t\tsetHoldAnimPrefix(${d.key}, \"${d.color} hold piece\");\n")
                        hxc.append("\t\tsetEndAnimPrefix(${d.key}, \"${d.color} hold end\");\n")
                    }
                }
                hxc.append("\t}\n\n}\n")
                text("data/uiSkins/note/$cls.hxc", hxc.toString())
                skin.put("note", cls)
            } else skin.put("note", "Default")
            if (style.hasStrums) {
                val arrows = JSONObject()
                NoteDir.entries.forEach { d ->
                    val s = style.strums.getValue(d)
                    arrows.put(d.key, JSONObject()
                        .put("idle", JSONObject().put("prefix", "arrow${d.upper}").put("framerate", 0).put("offset", JSONArray(listOf(0, 0))))
                        .put("pressed", JSONObject().put("prefix", "${d.key} press").put("framerate", (s.press?.fps ?: 24.0).roundToInt()).put("offset", JSONArray(listOf(0, 0))))
                        .put("confirm", JSONObject().put("prefix", "${d.key} confirm").put("framerate", (s.confirm?.fps ?: 24.0).roundToInt()).put("offset", JSONArray(listOf(0, 0)))))
                }
                val hud = JSONObject().put("path", path).put("frameLoadType", JSONObject().put("type", "sparrow")).put("scale", round(style.strumScale)).put("spacing", 4)
                    .put("antialiasing", !style.pixel).put("splash", if (style.hasSplashes) cls else "Default").put("holdCover", "Default").put("arrows", arrows)
                text("data/uiSkins/hudNote/$cls.json", hud.toString(2))
                skin.put("playerNotes", cls).put("opponentNotes", cls)
            } else skin.put("playerNotes", "Default").put("opponentNotes", "Default")
            if (style.hasSplashes) {
                val builder = SheetBuilder(1.0, !style.pixel)
                val animations = JSONObject()
                NoteDir.entries.forEach { d ->
                    val list = JSONArray()
                    style.splashes[d].orEmpty().forEachIndexed { i, anim ->
                        val prefix = "note impact ${i + 1} ${d.color}"
                        builder.add(prefix, anim, splashBake(d, true))
                        val (_, _, c) = splashCenter(anim)
                        list.put(JSONObject().put("prefix", prefix)
                            .put("framerateRange", JSONArray(listOf((anim.fps - anim.variance).roundToInt(), (anim.fps + anim.variance).roundToInt())))
                            .put("offset", JSONArray(listOf(round(c.first), round(c.second)))))
                    }
                    animations.put(d.key, list)
                }
                sheet(builder, "ui/notes/${style.id}-splashes")
                val splash = JSONObject().put("path", "ui/notes/${style.id}-splashes").put("randomRotation", false).put("limitedRotationAngles", false)
                    .put("alpha", round(style.splashAlpha)).put("antialiasing", !style.pixel).put("scale", round(style.splashScale)).put("animations", animations)
                text("data/uiSkins/noteSplash/$cls.json", splash.toString(2))
            }
            text("data/uiSkins/$cls.json", skin.toString(2))
            if (style.rgb != null || style.splashRgb != null) lines += "FPS Plus has no note color shader, so the colors were painted into the images."
            lines += "Use it with uiSkin = \"$cls\" in the stage class (or in a song script before the HUD is made)."
            return result("FPS Plus UI skin $cls")
        }
    }
}
