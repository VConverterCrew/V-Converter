package com.vconvertercrew.vconverter.convert.character

import com.vconvertercrew.vconverter.json.JSONObject

object BaseCharacterScripts {
    data class Script(val file: String, val className: String)

    val scripts: Map<String, Script> = mapOf(
        "bf" to Script("bf", "BoyfriendCharacter"),
        "bf-dark" to Script("bf-dark", "BoyfriendDarkCharacter"),
        "bf-christmas" to Script("bf-christmas", "BoyfriendChristmasCharacter"),
        "bf-pixel" to Script("bf-pixel", "BFPixelCharacter"),
        "bf-holding-gf" to Script("bf-holding-gf", "BFHoldingGFCharacter"),
        "gf" to Script("gf", "GirlfriendCharacter"),
        "gf-dark" to Script("gf-dark", "GirlfriendDarkCharacter"),
        "parents-christmas" to Script("parents-christmas", "ParentsChristmasCharacter"),
        "monster" to Script("monster", "MonsterCharacter"),
        "spooky-dark" to Script("spooky-dark", "SpookyDarkCharacter"),
        "spirit" to Script("spirit", "SpiritCharacter"),
        "tankman" to Script("tankman", "TankmanCharacter"),
        "tankman-bloody" to Script("tankman-bloody", "TankmanBloodyCharacter"),
        "darnell" to Script("darnell", "DarnellCharacter"),
        "darnell-blazin" to Script("darnell-blazin", "DarnellBlazinCharacter"),
        "nene-pixel" to Script("nene-pixel", "NenePixelCharacter"),
        "nene-dark" to Script("nene-dark", "NeneDarkCharacter"),
        "nene-tankmen" to Script("nene-tankmen", "NeneTankmenCharacter"),
        "nene-christmas" to Script("nene-christmas", "NeneChristmasCharacter"),
        "pico-christmas" to Script("pico-christmas", "PicoChristmasCharacter"),
        "pico-holding-nene" to Script("pico-holding-nene", "PicoHoldingNeneCharacter"),
        "pico-blazin" to Script("pico-blazin", "PicoBlazinCharacter"),
        "pico-pixel" to Script("pico-pixel", "PicoPixelCharacter"),
        "pico-speaker" to Script("pico-speaker", "PicoSpeakerCharacter"),
        "pico-dark" to Script("pico-dark", "PicoDarkCharacter"),
        "otis-speaker" to Script("otis-speaker", "OtisSpeakerCharacter"),
        "sserafim-sakura" to Script("sserafim-sakura", "SserafimSakuraCharacter"),
        "sserafim-yunjin" to Script("sserafim-yunjin", "SserafimYunjinCharacter"),
        "sserafim-eunchae" to Script("sserafim-eunchae", "SserafimEunchaeCharacter"),
        "sserafim-kazuha" to Script("sserafim-kazuha", "SserafimKazuhaCharacter"),
        "sserafim-gf" to Script("sserafim-gf", "SserafimGirlfriendCharacter"),
        "sserafim-chaewon" to Script("sserafim-chaewon", "SserafimChaewonCharacter")
    )

    private val bases = mapOf(
        "sparrow" to "SparrowCharacter",
        "multisparrow" to "MultiSparrowCharacter",
        "packer" to "PackerCharacter",
        "animateatlas" to "AnimateAtlasCharacter",
        "multianimateatlas" to "MultiAnimateAtlasCharacter"
    )

    fun replacement(id: String, characterJson: String): Pair<String, String>? {
        val script = scripts[id] ?: return null
        val render = runCatching { JSONObject(characterJson).optString("renderType", "sparrow") }.getOrDefault("sparrow").lowercase()
        val base = bases[render] ?: "SparrowCharacter"
        val text = buildString {
            append("import funkin.play.character.").append(base).append(";\n\n")
            append("class ").append(script.className).append(" extends ").append(base).append("\n{\n")
            append("  public function new()\n  {\n")
            append("    super(\"").append(id).append("\");\n")
            append("  }\n}\n")
        }
        return "scripts/characters/${script.file}.hxc" to text
    }

    fun note(id: String): String =
        "Character \"$id\" replaces a base game character that has its own script; scripts/characters/${scripts[id]?.file}.hxc was replaced so the base script doesn't run on the new sprites."
}
