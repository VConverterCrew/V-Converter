package com.vconvertercrew.vconverter.notestyle

import com.vconvertercrew.vconverter.editor.SpriteAtlas
import com.vconvertercrew.vconverter.json.JSONArray
import com.vconvertercrew.vconverter.json.JSONObject

class NoteStyleFiles(val inputs: List<NoteStyleInput>) {
    val missing = LinkedHashSet<String>()

    fun clean(path: String): String = path.trim().replace('\\', '/').substringAfter(':').trim('/')

    fun find(relative: String, ext: String): NoteStyleInput? {
        val want = (clean(relative).removeSuffix(".$ext") + ".$ext").lowercase()
        inputs.firstOrNull { val p = it.path.lowercase(); p == want || p.endsWith("/$want") }?.let { return it }
        val name = want.substringAfterLast('/')
        return inputs.filter { it.path.lowercase().substringAfterLast('/') == name }.singleOrNull()
    }

    fun sheet(relative: String): Sheet? {
        val path = clean(relative).removeSuffix(".png").removeSuffix(".xml")
        val xml = find(path, "xml")?.text
        val png = find(path, "png")?.image
        if (xml == null || png == null) {
            missing += "images/$path." + if (xml == null && png == null) "png and .xml" else if (xml == null) "xml" else "png"
            return null
        }
        return Sheet(path, png, SpriteAtlas.parse(xml))
    }

    fun image(relative: String): com.vconvertercrew.vconverter.core.RgbaImage? {
        val path = clean(relative).removeSuffix(".png")
        return find(path, "png")?.image ?: run {
            missing += "images/$path.png"
            null
        }
    }

    fun texts(ext: String): List<NoteStyleInput> = inputs.filter { it.text != null && it.path.lowercase().endsWith(".$ext") }

    companion object {
        fun imagePath(path: String): String {
            val clean = path.replace('\\', '/')
            val at = clean.lowercase().lastIndexOf("images/")
            val rest = if (at >= 0) clean.substring(at + 7) else clean.substringAfterLast('/')
            return rest.substringBeforeLast('.')
        }

        fun slug(text: String): String = text.lowercase().replace(Regex("[^a-z0-9]+"), "-").trim('-').ifEmpty { "notes" }
    }
}

object NoteStyleReaders {
    fun read(engine: String, files: NoteStyleFiles): List<NoteStyle> = when (engine) {
        "vslice" -> readVSlice(files)
        "nmv" -> readNmv(files)
        "codename" -> readSheets(files, codename = true)
        else -> readSheets(files, codename = false)
    }

    private fun isNoteSheet(atlas: SpriteAtlas): Boolean =
        NoteDir.entries.all { atlas.framesFor(it.color + "0").isNotEmpty() } || NoteDir.entries.all { atlas.framesFor("arrow" + it.upper).isNotEmpty() }

    private fun isSplashSheet(atlas: SpriteAtlas): Boolean = atlas.frames.any { it.name.startsWith("note splash") || it.name.startsWith("note impact") }

    private fun postfix(path: String): String {
        val name = path.substringAfterLast('/')
        return when {
            name.contains('-') -> name.substringAfter('-')
            name.equals("NOTE_assets", true) || name.equals("noteSplashes", true) || name.equals("default", true) -> ""
            else -> name
        }.lowercase()
    }

    private fun psychSkin(sheet: Sheet, codename: Boolean): Triple<Map<NoteDir, Anim>, Map<NoteDir, StrumAnims>, Map<NoteDir, HoldAnims>> {
        val notes = NoteDir.entries.mapNotNull { dir -> Anim.of(sheet, dir.color + "0")?.let { dir to it } }.toMap()
        val strums = NoteDir.entries.mapNotNull { dir ->
            Anim.of(sheet, "arrow" + dir.upper)?.let { dir to StrumAnims(it, Anim.of(sheet, "${dir.key} press"), Anim.of(sheet, "${dir.key} confirm")) }
        }.toMap()
        val holds = NoteDir.entries.mapNotNull { dir ->
            val piece = Anim.of(sheet, "${dir.color} hold piece", looped = true)
            val end = Anim.of(sheet, "${dir.color} hold end", looped = true) ?: if (dir == NoteDir.LEFT) Anim.of(sheet, "pruple end hold", looped = true) else null
            if (piece != null && end != null) dir to HoldAnims(piece, end) else null
        }.toMap()
        return Triple(notes, strums, holds)
    }

    private class SplashSet(val key: String, val anims: Map<NoteDir, List<Anim>>, val scale: Double, val alpha: Double, val rgb: List<Rgb>?, val info: List<String>)

    private fun scanSplashes(sheet: Sheet, base: String, fps: Double, variance: Double): Map<NoteDir, List<Anim>> = NoteDir.entries.associateWith { dir ->
        val list = ArrayList<Anim>()
        for (n in 1..9) {
            val anim = Anim.of(sheet, "$base ${dir.color} $n", fps, variance = variance)
                ?: Anim.of(sheet, "note impact $n ${dir.color}", fps, variance = variance)
                ?: Anim.of(sheet, "note impact $n  ${dir.color}", fps, variance = variance)
                ?: break
            list += anim
        }
        list
    }

    private fun psychSplash(files: NoteStyleFiles, sheet: Sheet, jsonText: String?, txtText: String?): SplashSet {
        val info = ArrayList<String>()
        var scale = 1.0
        var allowRgb = true
        var colors: List<Rgb>? = null
        val anims: Map<NoteDir, List<Anim>>
        val json = jsonText?.let { runCatching { JSONObject(it) }.getOrNull() }
        if (json != null && json.optJSONObject("animations") != null) {
            val list = json.getJSONObject("animations")
            scale = json.optDouble("scale", 1.0).takeIf { !it.isNaN() && it > 0 } ?: 1.0
            allowRgb = json.optBoolean("allowRGB", true)
            colors = json.optJSONArray("rgb")?.let { readPsychSplashRgb(it) }
            val grouped = HashMap<NoteDir, MutableList<Pair<Int, Anim>>>()
            list.keys().forEach { key ->
                val a = list.optJSONObject(key) ?: return@forEach
                val data = a.optInt("noteData", 0)
                val fps = a.optJSONArray("fps")
                val low = fps?.optDouble(0, 24.0) ?: 24.0
                val high = fps?.optDouble(1, low) ?: low
                val indices = a.optJSONArray("indices")?.let { arr -> (0 until arr.length()).map { arr.optInt(it) } }.orEmpty()
                val anim = Anim.of(sheet, a.optString("prefix"), (low + high) / 2, indices = indices, variance = (high - low) / 2)
                if (anim == null) {
                    info += "Splash animation \"${a.optString("prefix")}\" isn't in images/${sheet.path}.xml."
                    return@forEach
                }
                grouped.getOrPut(NoteDir.entries[((data % 4) + 4) % 4]) { mutableListOf() } += data to anim
            }
            anims = NoteDir.entries.associateWith { dir -> grouped[dir].orEmpty().sortedBy { it.first }.map { it.second } }
        } else {
            var base = "note splash"
            var low = 22.0
            var high = 26.0
            txtText?.lines()?.map { it.trim() }?.filter { it.isNotEmpty() }?.let { lines ->
                lines.getOrNull(0)?.let { base = it }
                lines.getOrNull(1)?.split(' ')?.let { parts ->
                    low = parts.getOrNull(0)?.toDoubleOrNull() ?: low
                    high = parts.getOrNull(1)?.toDoubleOrNull() ?: high
                }
            }
            anims = scanSplashes(sheet, base, (low + high) / 2, (high - low) / 2)
        }
        val template = allowRgb && isSplashTemplate(anims)
        val rgb = if (template) NoteDir.entries.map { dir -> colors?.getOrNull(dir.ordinal) ?: NoteColors.psych[dir.ordinal] } else null
        if (template) info += "The splashes are a Psych RGB template, colored in game like the notes."
        return SplashSet(postfix(sheet.path), anims, scale, 0.6, rgb, info)
    }

    private fun readPsychSplashRgb(array: JSONArray): List<Rgb>? {
        val first = array.optJSONObject(0) ?: return null
        fun color(key: String, fallback: Int) = first.opt(key)?.let { parseColor(it) } ?: fallback
        return NoteDir.entries.map { dir ->
            val base = NoteColors.psych[dir.ordinal]
            Rgb(color("r", base.r), color("g", base.g), color("b", base.b))
        }.takeIf { array.length() > 0 }
    }

    private fun isSplashTemplate(anims: Map<NoteDir, List<Anim>>): Boolean {
        val firsts = NoteDir.entries.associateWith { anims[it]?.firstOrNull() }
        if (firsts.values.any { it == null }) return false
        return NoteImages.isTemplate(firsts.mapValues { it.value!! })
    }

    fun parseColor(value: Any?): Int? = when (value) {
        is Number -> value.toLong().toInt()
        is String -> {
            val t = value.trim().removePrefix("#").removePrefix("0x").removePrefix("0X")
            t.toLongOrNull(16)?.let { if (t.length <= 6) (it or 0xFF000000L).toInt() else it.toInt() }
        }
        else -> null
    }

    private fun codenameSplash(files: NoteStyleFiles, input: NoteStyleInput): SplashSet? {
        val xml = input.text ?: return null
        val root = Regex("<splashes\\b([^>]*)>", RegexOption.IGNORE_CASE).find(xml) ?: return null
        val attrs = attrs(root.groupValues[1])
        val sprite = attrs["sprite"] ?: return null
        val sheet = files.sheet(sprite) ?: return null
        val info = ArrayList<String>()
        val shared = ArrayList<Anim>()
        val strumAnims = HashMap<NoteDir, MutableList<Anim>>()
        val strumBlock = Regex("<strum\\b([^>]*)>(.*?)</strum>", setOf(RegexOption.IGNORE_CASE, RegexOption.DOT_MATCHES_ALL))
        val animTag = Regex("<anim\\b([^>]*)/?>", RegexOption.IGNORE_CASE)
        fun anim(a: Map<String, String>): Anim? {
            val prefix = a["anim"] ?: a["name"] ?: return null
            val indices = a["indices"]?.split(',')?.mapNotNull { it.trim().toIntOrNull() }.orEmpty()
            return Anim.of(sheet, prefix, a["fps"]?.toDoubleOrNull() ?: 24.0, indices = indices).also {
                if (it == null) info += "Splash animation \"$prefix\" isn't in images/${sheet.path}.xml."
            }
        }
        var rest = xml
        strumBlock.findAll(xml).forEach { m ->
            val id = attrs(m.groupValues[1])["id"]?.toIntOrNull() ?: return@forEach
            val dir = NoteDir.entries.getOrNull(((id % 4) + 4) % 4) ?: return@forEach
            animTag.findAll(m.groupValues[2]).forEach { a -> anim(attrs(a.groupValues[1]))?.let { strumAnims.getOrPut(dir) { mutableListOf() } += it } }
            rest = rest.replace(m.value, "")
        }
        animTag.findAll(rest).forEach { a -> anim(attrs(a.groupValues[1]))?.let { shared += it } }
        val anims = NoteDir.entries.associateWith { strumAnims[it].orEmpty() + shared }
        val name = input.path.substringAfterLast('/').substringBeforeLast('.')
        return SplashSet(name.lowercase(), anims, attrs["scale"]?.toDoubleOrNull() ?: 1.0, attrs["alpha"]?.toDoubleOrNull() ?: 1.0, null, info)
    }

    private fun attrs(text: String): Map<String, String> = Regex("([A-Za-z_:][\\w:.-]*)\\s*=\\s*(\"([^\"]*)\"|'([^']*)')").findAll(text).associate {
        it.groupValues[1].lowercase() to it.groupValues[3].ifEmpty { it.groupValues[4] }
    }

    private fun readSheets(files: NoteStyleFiles, codename: Boolean): List<NoteStyle> {
        val skins = ArrayList<Sheet>()
        val splashes = ArrayList<SplashSet>()
        files.texts("xml").forEach { input ->
            val text = input.text ?: return@forEach
            if (codename && Regex("<splashes\\b", RegexOption.IGNORE_CASE).containsMatchIn(text)) {
                codenameSplash(files, input)?.let { splashes += it }
                return@forEach
            }
            if (!text.contains("<TextureAtlas", true)) return@forEach
            val atlas = SpriteAtlas.parse(text)
            val path = NoteStyleFiles.imagePath(input.path)
            when {
                isNoteSheet(atlas) -> files.sheet(path)?.let { skins += it }
                !codename && (isSplashSheet(atlas) || path.contains("notesplash", true) || files.find(path, "json")?.text?.contains("\"animations\"") == true) -> files.sheet(path)?.let { sheet ->
                    val json = files.find(path, "json")?.text
                    val txt = files.find(path, "txt")?.text
                    splashes += psychSplash(files, sheet, json, txt)
                }
            }
        }
        val used = HashSet<SplashSet>()
        val styles = skins.map { sheet ->
            val (notes, strums, holds) = psychSkin(sheet, codename)
            val key = postfix(sheet.path)
            val splash = splashes.firstOrNull { it.key == key } ?: splashes.singleOrNull()
            splash?.let { used += it }
            val info = ArrayList<String>()
            val template = !codename && NoteImages.isTemplate(notes)
            if (template) info += "images/${sheet.path} is a Psych RGB template: its notes are gray and Psych colors them in game with the default note colors."
            if (!NoteDir.entries.all { it in holds }) info += "images/${sheet.path} has no hold note animations (purple hold piece…), so holds keep the default look."
            if (!NoteDir.entries.all { it in strums }) info += "images/${sheet.path} has no strum animations (arrowLEFT…), so the strumline keeps the default look."
            val name = sheet.path.substringAfterLast('/').let { if (it.contains('-')) it.substringAfter('-') else it }
            NoteStyle(
                id = NoteStyleFiles.slug((if (codename) "cne-" else "psych-") + name),
                name = name,
                author = "",
                notes = notes,
                strums = strums,
                holds = holds,
                splashes = splash?.anims.orEmpty(),
                splashScale = splash?.scale ?: 1.0,
                splashAlpha = splash?.alpha ?: 0.6,
                rgb = if (template) NoteColors.psych else null,
                splashRgb = splash?.rgb,
                info = info + splash?.info.orEmpty()
            )
        }
        val alone = splashes.filter { it !in used }.map { splash ->
            NoteStyle(
                id = NoteStyleFiles.slug((if (codename) "cne-" else "psych-") + splash.key.ifEmpty { "splashes" } + "-splashes"),
                name = splash.key.ifEmpty { "Splashes" },
                author = "",
                notes = emptyMap(),
                strums = emptyMap(),
                holds = emptyMap(),
                splashes = splash.anims,
                splashScale = splash.scale,
                splashAlpha = splash.alpha,
                splashRgb = splash.rgb,
                info = splash.info
            )
        }
        return styles + alone
    }

    private fun readVSlice(files: NoteStyleFiles): List<NoteStyle> = files.texts("json").mapNotNull { input ->
        val json = runCatching { JSONObject(input.text!!) }.getOrNull() ?: return@mapNotNull null
        val assets = json.optJSONObject("assets") ?: return@mapNotNull null
        val id = input.path.substringAfterLast('/').substringBeforeLast('.')
        val info = ArrayList<String>()
        fun prefix(o: JSONObject?, key: String): JSONObject? = o?.optJSONObject("data")?.optJSONObject(key)
        fun anim(sheet: Sheet?, data: JSONObject?, looped: Boolean = false): Anim? {
            if (sheet == null || data == null) return null
            val indices = data.optJSONArray("frameIndices")?.let { arr -> (0 until arr.length()).map { arr.optInt(it) } }.orEmpty()
            return Anim.of(sheet, data.optString("prefix"), data.optDouble("frameRate", 24.0).takeIf { !it.isNaN() } ?: 24.0, data.optBoolean("looped", looped), indices).also {
                if (it == null && data.optString("prefix").isNotEmpty()) info += "Animation \"${data.optString("prefix")}\" isn't in images/${sheet.path}.xml."
            }
        }
        val note = assets.optJSONObject("note")
        val noteSheet = note?.optString("assetPath")?.takeIf { it.isNotBlank() }?.let { files.sheet(it) }
        val notes = NoteDir.entries.mapNotNull { dir -> anim(noteSheet, prefix(note, dir.key))?.let { dir to it } }.toMap()
        val strumline = assets.optJSONObject("noteStrumline")
        val strumSheet = strumline?.optString("assetPath")?.takeIf { it.isNotBlank() }?.let { p -> if (noteSheet != null && files.clean(p) == noteSheet.path) noteSheet else files.sheet(p) }
        val strums = NoteDir.entries.mapNotNull { dir ->
            anim(strumSheet, prefix(strumline, dir.key + "Static"))?.let { dir to StrumAnims(it, anim(strumSheet, prefix(strumline, dir.key + "Press")), anim(strumSheet, prefix(strumline, dir.key + "Confirm"))) }
        }.toMap()
        val hold = assets.optJSONObject("holdNote")
        val pixel = note?.optBoolean("isPixel", false) == true || hold?.optBoolean("isPixel", false) == true
        val holds = hold?.optString("assetPath")?.takeIf { it.isNotBlank() }?.let { p -> files.image(p)?.let { HoldStrip.read(files.clean(p), it, hold.optBoolean("isPixel", false)) } }.orEmpty()
        val splash = assets.optJSONObject("noteSplash")
        val splashSheet = splash?.optString("assetPath")?.takeIf { it.isNotBlank() }?.let { files.sheet(it) }
        val splashData = splash?.optJSONObject("data")
        val splashFps = splashData?.optDouble("framerateDefault", 24.0)?.takeIf { !it.isNaN() } ?: 24.0
        val splashVariance = splashData?.optDouble("framerateVariance", 2.0)?.takeIf { !it.isNaN() } ?: 2.0
        val splashes = NoteDir.entries.associateWith { dir ->
            val list = splashData?.optJSONArray(dir.key + "Splashes") ?: return@associateWith emptyList()
            (0 until list.length()).mapNotNull { i ->
                val d = list.optJSONObject(i) ?: return@mapNotNull null
                anim(splashSheet, d)?.let { Anim(it.sheet, it.frames, d.optDouble("frameRate", splashFps).takeIf { f -> !f.isNaN() } ?: splashFps, false, splashVariance) }
            }
        }
        val fallback = json.optString("fallback", "")
        if (notes.size < 4 && fallback.isNotBlank()) info += "Notes come from the \"$fallback\" note style, so they keep the target's default look."
        if (assets.has("holdNoteCover")) info += "Hold covers have no equivalent outside V-Slice and were left out."
        if (listOf("countdownThree", "judgementSick", "comboNumber0").any { assets.has(it) }) info += "Countdown, judgement and combo sprites are V-Slice only and were left out."
        NoteStyle(
            id = NoteStyleFiles.slug(id),
            name = json.optString("name").ifBlank { id },
            author = json.optString("author"),
            notes = notes,
            strums = strums,
            holds = holds,
            splashes = splashes,
            noteScale = note?.optDouble("scale", 1.0)?.takeIf { !it.isNaN() } ?: 0.7,
            strumScale = strumline?.optDouble("scale", 1.0)?.takeIf { !it.isNaN() } ?: 0.7,
            holdScale = hold?.optDouble("scale", 1.0)?.takeIf { !it.isNaN() } ?: 0.7,
            splashScale = splash?.optDouble("scale", 1.0)?.takeIf { !it.isNaN() } ?: 1.0,
            splashAlpha = splash?.optDouble("alpha", 1.0)?.takeIf { !it.isNaN() } ?: 1.0,
            pixel = pixel,
            info = info
        )
    }

    private fun readNmv(files: NoteStyleFiles): List<NoteStyle> = files.texts("json").mapNotNull { input ->
        val json = runCatching { JSONObject(input.text!!) }.getOrNull() ?: return@mapNotNull null
        if (!listOf("noteTexture", "noteAnimations", "receptorAnimations", "splashTexture", "noteSplashAnimations").any { json.has(it) }) return@mapNotNull null
        val id = input.path.substringAfterLast('/').substringBeforeLast('.')
        val info = ArrayList<String>()
        val noteSheet = files.sheet(json.optString("noteTexture").ifBlank { "UI/notes/NOTE_assets" })
        fun anim(sheet: Sheet?, o: JSONObject?, looped: Boolean = false): Anim? {
            if (sheet == null || o == null) return null
            return Anim.of(sheet, o.optString("xmlName"), o.optDouble("fps", 24.0).takeIf { !it.isNaN() } ?: 24.0, o.optBoolean("looping", looped))
        }
        fun group(key: String, dir: NoteDir): Map<String, JSONObject> {
            val arr = json.optJSONArray(key)?.optJSONArray(dir.ordinal) ?: return emptyMap()
            return (0 until arr.length()).mapNotNull { arr.optJSONObject(it) }.associateBy { it.optString("anim") }
        }
        val defaults = noteSheet?.let { psychSkin(it, false) }
        val notes = NoteDir.entries.mapNotNull { dir -> (group("noteAnimations", dir)["scroll"]?.let { anim(noteSheet, it) } ?: defaults?.first?.get(dir))?.let { dir to it } }.toMap()
        val holds = NoteDir.entries.mapNotNull { dir ->
            val g = group("noteAnimations", dir)
            val piece = g["hold"]?.let { anim(noteSheet, it, true) } ?: defaults?.third?.get(dir)?.piece
            val end = g["holdend"]?.let { anim(noteSheet, it, true) } ?: defaults?.third?.get(dir)?.end
            if (piece != null && end != null) dir to HoldAnims(piece, end) else null
        }.toMap()
        val strums = NoteDir.entries.mapNotNull { dir ->
            val g = group("receptorAnimations", dir)
            val fallback = defaults?.second?.get(dir)
            val static = g["static"]?.let { anim(noteSheet, it) } ?: fallback?.static ?: return@mapNotNull null
            dir to StrumAnims(static, g["pressed"]?.let { anim(noteSheet, it) } ?: fallback?.press, g["confirm"]?.let { anim(noteSheet, it) } ?: fallback?.confirm)
        }.toMap()
        val splashSheet = if (json.optBoolean("splashesEnabled", true)) files.sheet(json.optString("splashTexture").ifBlank { "UI/notes/noteSplashes" }) else null
        val splashList = json.optJSONArray("noteSplashAnimations")
        val splashes = NoteDir.entries.associateWith { dir ->
            val o = splashList?.optJSONObject(dir.ordinal)
            listOfNotNull(anim(splashSheet, o) ?: splashSheet?.let { Anim.of(it, "note splash ${dir.color}") ?: Anim.of(it, "note splash") })
        }
        val coloring = json.optBoolean("inGameColoring", true)
        val colors = json.optJSONArray("arrowRGB")?.let { arr ->
            NoteDir.entries.map { dir ->
                val o = arr.optJSONObject(dir.ordinal)
                val base = NoteColors.psych[dir.ordinal]
                Rgb(parseColor(o?.opt("r")) ?: base.r, parseColor(o?.opt("g")) ?: base.g, parseColor(o?.opt("b")) ?: base.b)
            }
        } ?: NoteColors.psych
        val template = coloring && NoteImages.isTemplate(notes)
        val splashTemplate = coloring && splashes.values.all { it.isNotEmpty() } && isSplashTemplate(splashes)
        if (template) info += "The noteskin is an RGB template colored in game (inGameColoring)."
        NoteStyle(
            id = NoteStyleFiles.slug("nmv-$id"),
            name = id,
            author = "",
            notes = notes,
            strums = strums,
            holds = holds,
            splashes = splashes,
            noteScale = json.optDouble("noteScale", 0.7).takeIf { !it.isNaN() } ?: 0.7,
            strumScale = json.optDouble("receptorScale", 0.7).takeIf { !it.isNaN() } ?: 0.7,
            holdScale = json.optDouble("noteScale", 0.7).takeIf { !it.isNaN() } ?: 0.7,
            splashScale = json.optDouble("splashScale", 1.0).takeIf { !it.isNaN() } ?: 1.0,
            splashAlpha = json.optDouble("splashAlpha", 0.6).takeIf { !it.isNaN() } ?: 0.6,
            rgb = if (template) colors else null,
            splashRgb = if (splashTemplate) colors else null,
            info = info
        )
    }
}
