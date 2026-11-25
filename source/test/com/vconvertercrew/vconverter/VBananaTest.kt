package com.vconvertercrew.vconverter

import com.vconvertercrew.vconverter.tools.online.GameBanana
import com.vconvertercrew.vconverter.tools.online.VBananaNative
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class VBananaTest {
    private val psych = """{"_idRow":2,"_sModelName":"Mod","_sName":"VS Someone","_aGame":{"_idRow":8694},"_aSuperCategory":{"_sName":"Executables"},
        "_sDescription":"A cool mod","_aRequirements":[["Psych Engine 0.7.3","https://github.com/ShadowMario/FNF-PsychEngine"]],
        "_aCredits":[{"_sGroupName":"Team","_aAuthors":[{"_sRole":"Coder","_sName":"Ana"}]}],
        "_aFiles":[{"_idRow":9,"_sFile":"vs_someone_mod_folder.zip","_nFilesize":100,"_tsDateAdded":1,"_sAnalysisResult":"ok","_sAvResult":"clean"},
                   {"_idRow":10,"_sFile":"vs_someone_windows.zip","_nFilesize":100,"_tsDateAdded":2,"_sAnalysisResult":"ok","_sAvResult":"clean"}]}"""

    @Test
    fun detectsEngineAndPicksTheModFolder() {
        if (System.getProperty("vconverter.banana.library") == null) return
        assertTrue(VBananaNative.available)
        val details = GameBanana.parseDetails(psych)
        assertEquals("Psych Engine 0.7.3", details.engine)
        assertTrue(details.detection!!.confidence >= 60)
        assertEquals(9, GameBanana.bestFile(details.files)!!.id)
        assertEquals("mod", details.files.first { it.id == 9 }.role)
        assertEquals("build", details.files.first { it.id == 10 }.role)
        assertEquals("Psych Engine 0.7.3", details.requirements.single().title)
        assertEquals("Ana" to "Coder", details.credits.single().authors.single())
        assertEquals("A cool mod", details.description)
    }

    @Test
    fun readsUpdatesAndArchives() {
        val updates = GameBanana.parseUpdates(
            """{"_aMetadata":{"_nRecordCount":1},"_aRecords":[{"_idRow":7,"_sName":"Fixes","_sVersion":"1.1","_tsDateAdded":5,"_sText":"<b>Fixed</b> it",
                "_aChangeLog":[{"text":"Crash fix","cat":"Bugfix"}],"_aFileRowIds":[3]}]}"""
        )
        assertEquals(1, updates.size)
        assertEquals("1.1", updates[0].version)
        assertEquals("Bugfix" to "Crash fix", updates[0].changes.single())
        assertEquals(listOf(3), updates[0].files)
        assertEquals("Fixed it", updates[0].text)
        if (System.getProperty("vconverter.banana.library") == null) return
        val inspection = GameBanana.inspect(listOf("Build/PsychEngine.exe", "Build/mods/vs-x/pack.json", "Build/mods/vs-x/data/tutorial/tutorial-hard.json"))!!
        assertEquals("Psych Engine", inspection.engine)
        assertEquals("Build/mods/vs-x/", inspection.root)
        assertTrue(inspection.build)
    }
}
