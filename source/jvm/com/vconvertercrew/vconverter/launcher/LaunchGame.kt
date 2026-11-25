package com.vconvertercrew.vconverter.launcher

enum class LaunchGame(
    val id: Int,
    val title: String,
    val engine: String,
    val packages: List<String>,
    val storePackage: String?,
    val download: String,
    val androidDownload: String,
    val metaFile: String
) {
    FUNKIN(
        0,
        "Friday Night Funkin'",
        "vslice",
        listOf("me.funkin.fnf"),
        "me.funkin.fnf",
        "https://ninja-muffin24.itch.io/funkin",
        "https://play.google.com/store/apps/details?id=me.funkin.fnf",
        "_polymod_meta.json"
    ),
    PSYCH(
        1,
        "Psych Engine",
        "psych",
        listOf("me.funkin.psychengine", "com.shadowmario.psychengine"),
        null,
        "https://github.com/ShadowMario/FNF-PsychEngine/releases",
        "https://github.com/BrenninhoTools/FNF-PsychEngine-Mobile/releases",
        "pack.json"
    );

    val testLabel: String get() = "Test In $title"

    val testNote: String get() = "Puts the mod in $title's mods folder and switches to the game."

    val turnOn: String get() = when (this) {
        FUNKIN -> "Turn the mod on in its Mods menu."
        PSYCH -> "It's already turned on; open Mods in the main menu to change its order."
    }

    companion object {
        const val FUNKIN_PACKAGE = "me.funkin.fnf"

        fun forEngine(key: String): LaunchGame? = entries.firstOrNull { it.engine.equals(key, ignoreCase = true) }
    }
}
