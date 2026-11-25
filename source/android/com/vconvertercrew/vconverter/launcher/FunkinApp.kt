package com.vconvertercrew.vconverter.launcher

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri

enum class FunkinOpen { OPENED, MISSING, FAILED }

object FunkinApp {
    private fun hasLaunch(context: Context, packageName: String): Boolean =
        if (NativeLauncher.loaded) NativeLauncher.installed(context, packageName)
        else context.packageManager.getLaunchIntentForPackage(packageName) != null

    fun installedPackage(context: Context, game: LaunchGame = LaunchGame.FUNKIN): String? =
        game.packages.firstOrNull { hasLaunch(context, it) }

    fun installed(context: Context, game: LaunchGame = LaunchGame.FUNKIN): Boolean = installedPackage(context, game) != null

    fun open(context: Context, game: LaunchGame = LaunchGame.FUNKIN): FunkinOpen {
        val packageName = installedPackage(context, game) ?: return FunkinOpen.MISSING
        val code = if (NativeLauncher.loaded) NativeLauncher.launchPackage(context, packageName) else fallbackOpen(context, packageName)
        return when (code) {
            NativeLauncher.OK -> FunkinOpen.OPENED
            NativeLauncher.NOT_FOUND -> FunkinOpen.MISSING
            else -> FunkinOpen.FAILED
        }
    }

    fun openStore(context: Context, game: LaunchGame = LaunchGame.FUNKIN): Boolean {
        val store = game.storePackage
        if (store != null && NativeLauncher.loaded && NativeLauncher.openStore(context, store) == NativeLauncher.OK) return true
        val links = if (store != null) listOf("market://details?id=$store", "https://play.google.com/store/apps/details?id=$store") else listOf(game.androidDownload)
        return links.any { link ->
            runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(link)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }.isSuccess
        }
    }

    private fun fallbackOpen(context: Context, packageName: String): Int {
        val intent = context.packageManager.getLaunchIntentForPackage(packageName) ?: return NativeLauncher.NOT_FOUND
        return try {
            context.startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED))
            NativeLauncher.OK
        } catch (e: ActivityNotFoundException) {
            NativeLauncher.FAILED
        } catch (e: SecurityException) {
            NativeLauncher.FAILED
        }
    }
}
