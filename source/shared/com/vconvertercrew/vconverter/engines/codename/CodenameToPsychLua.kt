package com.vconvertercrew.vconverter.engines.codename

import com.vconvertercrew.vconverter.convert.script.HaxeLuaScript
import com.vconvertercrew.vconverter.convert.script.HaxeToLua

object CodenameToPsychLua {
    fun eligible(path: String, source: String, context: CodenamePsychScriptContext): Boolean =
        HaxeToLua.available && source.isNotBlank() && context.hooks.isEmpty && "/extensions/" !in path &&
            !(path.startsWith("scripts/") && path.endsWith("Event.hx"))

    fun convert(path: String, fileName: String, source: String, target: CodenameScriptTarget, context: CodenamePsychScriptContext): Pair<String, HaxeLuaScript>? {
        if (!eligible(path, source, context)) return null
        val lua = HaxeToLua.convert(
            source,
            "codename",
            fileName,
            target.key,
            context.stageSprites,
            eventName = if (target.scope == CodenameScope.EVENT) target.key else "",
            noteType = if (target.scope == CodenameScope.NOTE_TYPE) CodenameToPsychChart.noteTypeFor(target.key) else ""
        ) ?: return null
        return HaxeToLua.luaPath(path) to lua
    }
}
