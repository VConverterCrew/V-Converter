package com.vconvertercrew.vconverter.launcher

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import com.vconvertercrew.vconverter.io.DocumentIO

object FunkinFolder {
    const val AUTHORITY = "${LaunchGame.FUNKIN_PACKAGE}.docprovider"
    private const val PREFS = "funkin"

    private fun key(game: LaunchGame): String = if (game == LaunchGame.FUNKIN) "modsTree" else "modsTree-${game.engine}"

    fun initialUri(context: Context, game: LaunchGame = LaunchGame.FUNKIN): Uri = when (game) {
        LaunchGame.FUNKIN -> DocumentsContract.buildDocumentUri(AUTHORITY, "/storage/emulated/0/Android/data/${LaunchGame.FUNKIN_PACKAGE}/files")
        LaunchGame.PSYCH -> DocumentsContract.buildRootUri("${FunkinApp.installedPackage(context, game) ?: game.packages.first()}.documents", "root")
    }

    fun saved(context: Context, game: LaunchGame = LaunchGame.FUNKIN): Uri? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(key(game), null)
            ?.let { runCatching { Uri.parse(it) }.getOrNull() }
            ?.takeIf { DocumentIO.hasAccess(context, it) }

    fun keep(context: Context, tree: Uri, game: LaunchGame = LaunchGame.FUNKIN) {
        DocumentIO.keepAccess(context, tree)
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(key(game), tree.toString()).apply()
    }

    fun forget(context: Context, game: LaunchGame = LaunchGame.FUNKIN) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().remove(key(game)).apply()
    }

    fun insideMods(context: Context, tree: Uri): Boolean =
        DocumentIO.folderName(context, tree).equals("mods", true)
}
