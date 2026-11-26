package com.vconvertercrew.vconverter.engines.luaslice

object LuaSlice {
    const val KEY = "luaslice"
    const val TITLE = "LuaSlice"
    const val ANDROID_PACKAGE = "me.luaslice"

    val psychExtensions = setOf("luap", "luapg")
    val nativeExtensions = setOf("lua", "luag")

    private val scriptCalls = Regex("""\b(addLuaScript|removeLuaScript|isRunning|callScript|setOnLuas|callOnLuas)\s*\(\s*(['"])([^'"\n]*?)\.(lua|luap|luapg)\2""")

    private val knownFolders = setOf("scripts", "data", "songs", "stages", "characters", "custom_events", "custom_notetypes")

    fun modRelative(path: String, fallbackFolder: String = "scripts"): String {
        val parts = path.replace('\\', '/').trim('/').split('/').filter { it.isNotEmpty() }
        val index = parts.indexOfFirst { it.lowercase() in knownFolders }
        return if (index >= 0 && index < parts.size - 1) parts.drop(index).joinToString("/") else "$fallbackFolder/${parts.last()}"
    }

    fun extension(path: String): String = path.substringAfterLast('/').substringAfterLast('.', "").lowercase()

    fun isPsychScript(path: String): Boolean = extension(path) in psychExtensions

    fun isNativeScript(path: String): Boolean = extension(path) in nativeExtensions

    fun toLuaSlicePath(rel: String): String {
        val clean = rel.trim('/')
        val parts = clean.split('/')
        val top = parts.first().lowercase()
        val stem = clean.substringBeforeLast('.')
        return when {
            top == "data" && parts.size == 3 -> "data/songs/${parts[1].lowercase()}/${parts[2].substringBeforeLast('.')}.luap"
            top == "data" && parts.size > 3 && parts[1].lowercase() == "songs" -> "$stem.luap"
            else -> "$stem.luap"
        }
    }

    fun toPsychPath(rel: String, songFolder: (String) -> String = { it }): String {
        val clean = rel.trim('/')
        val parts = clean.split('/')
        val top = parts.first().lowercase()
        val name = parts.last().substringBeforeLast('.')
        return when {
            parts.size == 1 -> "scripts/$name.lua"
            top == "data" && parts.size == 4 && parts[1].lowercase() == "songs" -> "data/${songFolder(parts[2])}/$name.lua"
            top == "songs" && parts.size == 3 -> "data/${songFolder(parts[1])}/$name.lua"
            top == "data" && parts.size == 3 -> "data/${songFolder(parts[1])}/$name.lua"
            else -> clean.substringBeforeLast('.') + ".lua"
        }
    }

    fun toLuaSliceText(text: String): String = scriptCalls.replace(text) { m ->
        val ext = m.groupValues[4].lowercase()
        if (ext != "lua") m.value else m.value.substring(0, m.value.length - 5) + ".luap" + m.groupValues[2]
    }

    fun toPsychText(text: String): String = scriptCalls.replace(text) { m ->
        val ext = m.groupValues[4].lowercase()
        if (ext == "lua") m.value else m.value.substring(0, m.value.length - ext.length - 2) + ".lua" + m.groupValues[2]
    }

    val notes = listOf(
        "Lua scripts stay in Lua as LuaSlice Psych scripts (.luap): scripts/, data/songs/<song>/, stages/<stage>.luap, characters/<character>.luap, custom_events/ and custom_notetypes/ keep their scope.",
        "Chart events keep their Psych names and value1/value2, so onEvent and custom_events/<name>.luap see the same names as in Psych; Change Character uses LuaSlice's ChangeCharacter event.",
        "Custom note types keep their Psych names as note kinds, so custom_notetypes/<type>.luap and noteType checks still match.",
        "HScript (.hx) became V-Slice .hxc scripts, which LuaSlice runs like V-Slice does."
    )
}
