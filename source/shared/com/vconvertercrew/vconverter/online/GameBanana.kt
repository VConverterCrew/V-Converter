package com.vconvertercrew.vconverter.online

import com.vconvertercrew.vconverter.json.JSONArray
import com.vconvertercrew.vconverter.json.JSONObject
import com.vconvertercrew.vconverter.platform.Http
import kotlin.time.TimeSource

class GameBananaException(message: String) : Exception(message)

data class GameBananaMod(
    val id: Int,
    val name: String,
    val submitter: String,
    val category: String,
    val likes: Int,
    val views: Int,
    val thumbnail: String?,
    val profileUrl: String,
    val hasFiles: Boolean,
    val version: String = "",
    val updated: Long = 0,
    val obsolete: Boolean = false,
    val engine: String? = null
)

data class GameBananaFile(
    val id: Int,
    val name: String,
    val size: Long,
    val description: String,
    val downloads: Int,
    val url: String,
    val md5: String,
    val added: Long,
    val clean: Boolean,
    val role: String = "",
    val engine: String? = null,
    val version: String = "",
    val score: Int = 0,
    val warning: String = ""
) {
    val isArchive: Boolean get() = GameBanana.archiveExtensions.any { name.lowercase().endsWith(it) }
}

data class GameBananaEngine(val name: String, val version: String, val confidence: Int, val reasons: List<String>) {
    val label: String get() = if (version.isEmpty()) name else "$name $version"
}

data class GameBananaLink(val title: String, val url: String)

data class GameBananaCredit(val group: String, val authors: List<Pair<String, String>>)

data class GameBananaUpdate(
    val id: Int,
    val name: String,
    val version: String,
    val added: Long,
    val text: String,
    val changes: List<Pair<String, String>>,
    val files: List<Int>
)

data class GameBananaInspection(
    val engine: String?,
    val confidence: Int,
    val root: String,
    val build: Boolean,
    val modFolders: List<String>,
    val songs: List<String>,
    val entries: Int,
    val lua: Int,
    val hxc: Int,
    val hx: Int
)

data class GameBananaDetails(
    val mod: GameBananaMod,
    val files: List<GameBananaFile>,
    val downloads: Int,
    val engine: String?,
    val description: String = "",
    val text: String = "",
    val license: String = "",
    val requirements: List<GameBananaLink> = emptyList(),
    val credits: List<GameBananaCredit> = emptyList(),
    val alternateSources: List<GameBananaLink> = emptyList(),
    val screenshots: List<String> = emptyList(),
    val detection: GameBananaEngine? = null,
    val bestId: Int = 0,
    val updates: Int = 0
)

data class GameBananaPage(val mods: List<GameBananaMod>, val total: Int, val complete: Boolean)

enum class GameBananaFeed(val label: String, val sort: String) {
    POPULAR("Popular", "default"),
    NEW("New", "new"),
    UPDATED("Updated", "updated")
}

object GameBanana {
    const val GAME_ID = 8694
    const val SITE = "https://gamebanana.com"
    const val API = "$SITE/apiv11"
    private const val TIMEOUT_MS = 15000

    val archiveExtensions = listOf(".zip", ".rar", ".7z")

    private val headers = mapOf("Accept" to "application/json", "User-Agent" to "V-Converter")

    private val engineWords = listOf(
        "v-slice" to "V-Slice", "vslice" to "V-Slice", "base game" to "V-Slice", "polymod" to "V-Slice",
        "psych" to "Psych Engine", "codename" to "Codename Engine", "nightmarevision" to "NightmareVision", "fps plus" to "FPS Plus",
        "kade" to "Kade Engine"
    )

    fun encode(text: String): String = buildString {
        text.encodeToByteArray().forEach { byte ->
            val c = (byte.toInt() and 0xFF).toChar()
            if (c.isLetterOrDigit() && c.code < 128 || c in "-_.~") append(c) else append('%').append(((byte.toInt() and 0xFF) or 0x100).toString(16).substring(1).uppercase())
        }
    }

    val native: Boolean get() = VBananaNative.available

    private fun nativeLink(text: String): Pair<String, Int>? {
        val raw = VBananaNative.link(text) ?: return null
        val json = JSONObject(raw)
        val kind = json.optString("kind")
        val id = json.optInt("id")
        return if (kind == "none" || id <= 0) null else kind to id
    }

    fun parseModId(text: String): Int? {
        if (native) return nativeLink(text)?.takeIf { it.first == "mod" || it.first == "wip" || it.first == "tool" }?.second
        val trimmed = text.trim()
        trimmed.toIntOrNull()?.let { return it.takeIf { id -> id > 0 } }
        val match = Regex("gamebanana\\.com/(?:mods|wips|tools|sounds)/(?:download/)?(\\d+)", RegexOption.IGNORE_CASE).find(trimmed)
            ?: Regex("gamebanana\\.com/.*[?&]_?id(?:Row)?=(\\d+)", RegexOption.IGNORE_CASE).find(trimmed)
        return match?.groupValues?.get(1)?.toIntOrNull()
    }

    fun parseFileId(text: String): Int? = if (native) nativeLink(text)?.takeIf { it.first == "file" }?.second else
        Regex("gamebanana\\.com/(?:dl|mmdl)/(\\d+)", RegexOption.IGNORE_CASE).find(text.trim())?.groupValues?.get(1)?.toIntOrNull()

    fun engineOf(text: String): String? {
        val lower = text.lowercase()
        return engineWords.firstOrNull { lower.contains(it.first) }?.second
    }

    private fun thumbnail(record: JSONObject): String? {
        val images = record.optJSONObject("_aPreviewMedia")?.optJSONArray("_aImages") ?: return null
        val first = images.optJSONObject(0) ?: return null
        val base = first.optString("_sBaseUrl")
        val file = first.optString("_sFile220").ifEmpty { first.optString("_sFile530") }.ifEmpty { first.optString("_sFile") }
        return if (base.isEmpty() || file.isEmpty()) null else "$base/$file"
    }

    fun parseMod(record: JSONObject): GameBananaMod? {
        val id = record.optInt("_idRow")
        if (id <= 0) return null
        if (record.optString("_sModelName", "Mod") != "Mod") return null
        val category = record.optJSONObject("_aRootCategory")?.optString("_sName")?.takeIf { it.isNotEmpty() }
            ?: record.optJSONObject("_aCategory")?.optString("_sName").orEmpty()
        return GameBananaMod(
            id = id,
            name = record.optString("_sName").ifBlank { "Mod $id" },
            submitter = record.optJSONObject("_aSubmitter")?.optString("_sName").orEmpty(),
            category = category,
            likes = record.optInt("_nLikeCount"),
            views = record.optInt("_nViewCount"),
            thumbnail = thumbnail(record),
            profileUrl = record.optString("_sProfileUrl").ifEmpty { "$SITE/mods/$id" },
            hasFiles = record.optBoolean("_bHasFiles", true)
        )
    }

    private fun nativeMod(json: JSONObject): GameBananaMod = GameBananaMod(
        id = json.optInt("id"),
        name = json.optString("name"),
        submitter = json.optString("submitter"),
        category = json.optString("category"),
        likes = json.optInt("likes"),
        views = json.optInt("views"),
        thumbnail = json.optString("thumbnail").ifEmpty { null },
        profileUrl = json.optString("profileUrl"),
        hasFiles = json.optBoolean("hasFiles", true),
        version = json.optString("version"),
        updated = json.optLong("updated"),
        obsolete = json.optBoolean("obsolete"),
        engine = json.optString("engine").ifEmpty { null }
    )

    private fun nativeFile(json: JSONObject): GameBananaFile = GameBananaFile(
        id = json.optInt("id"),
        name = json.optString("name"),
        size = json.optLong("size"),
        description = json.optString("description"),
        downloads = json.optInt("downloads"),
        url = json.optString("url"),
        md5 = json.optString("md5"),
        added = json.optLong("added"),
        clean = json.optBoolean("clean") && !json.optBoolean("flagged"),
        role = json.optString("role"),
        engine = json.optString("engine").ifEmpty { null },
        version = json.optString("version"),
        score = json.optInt("score"),
        warning = json.optString("warning")
    )

    private fun objects(array: JSONArray?): List<JSONObject> = array?.let { a -> (0 until a.length()).mapNotNull { a.optJSONObject(it) } }.orEmpty()

    private fun strings(array: JSONArray?): List<String> = array?.let { a -> (0 until a.length()).map { a.optString(it) } }.orEmpty()

    private fun nativeResult(kind: String, text: String): JSONObject? {
        val raw = VBananaNative.parse(kind, text) ?: return null
        val json = JSONObject(raw)
        if (!json.optBoolean("ok", true)) throw GameBananaException(json.optString("error").ifEmpty { "GameBanana sent something unexpected." })
        return json
    }

    fun parsePage(text: String): GameBananaPage {
        if (native) {
            nativeResult("page", text)?.let { json ->
                val mods = objects(json.optJSONArray("mods")).map(::nativeMod)
                return GameBananaPage(mods, json.optInt("total", mods.size), json.optBoolean("complete", true))
            }
        }
        val root = JSONObject(text)
        val records = root.optJSONArray("_aRecords") ?: JSONArray()
        val meta = root.optJSONObject("_aMetadata")
        val mods = (0 until records.length()).mapNotNull { records.optJSONObject(it)?.let(::parseMod) }
        return GameBananaPage(mods, meta?.optInt("_nRecordCount") ?: mods.size, meta?.optBoolean("_bIsComplete", true) ?: true)
    }

    fun parseFile(file: JSONObject): GameBananaFile? {
        val id = file.optInt("_idRow")
        if (id <= 0) return null
        val analysis = file.optString("_sAnalysisResult").lowercase()
        val av = file.optString("_sAvResult").lowercase()
        return GameBananaFile(
            id = id,
            name = file.optString("_sFile").ifBlank { "file-$id" },
            size = file.optLong("_nFilesize"),
            description = file.optString("_sDescription"),
            downloads = file.optInt("_nDownloadCount"),
            url = file.optString("_sDownloadUrl").ifEmpty { "$SITE/dl/$id" },
            md5 = file.optString("_sMd5Checksum"),
            added = file.optLong("_tsDateAdded"),
            clean = (analysis.isEmpty() || analysis == "ok") && (av.isEmpty() || av == "clean")
        )
    }

    fun parseDetails(text: String): GameBananaDetails {
        if (native) {
            nativeResult("mod", text)?.let { json ->
                val engine = json.optJSONObject("engine")
                val detection = engine?.optString("name")?.takeIf { it.isNotEmpty() }?.let {
                    GameBananaEngine(it, engine.optString("version"), engine.optInt("confidence"), strings(engine.optJSONArray("reasons")))
                }
                return GameBananaDetails(
                    mod = nativeMod(json.optJSONObject("mod") ?: JSONObject()),
                    files = objects(json.optJSONArray("files")).map(::nativeFile),
                    downloads = json.optInt("downloads"),
                    engine = detection?.label,
                    description = json.optString("description"),
                    text = json.optString("text"),
                    license = json.optString("license"),
                    requirements = objects(json.optJSONArray("requirements")).map { GameBananaLink(it.optString("name"), it.optString("url")) },
                    credits = objects(json.optJSONArray("credits")).map { group ->
                        GameBananaCredit(group.optString("group"), objects(group.optJSONArray("authors")).map { it.optString("name") to it.optString("role") })
                    },
                    alternateSources = objects(json.optJSONArray("alternateSources")).map { GameBananaLink(it.optString("description"), it.optString("url")) },
                    screenshots = strings(json.optJSONArray("screenshots")),
                    detection = detection,
                    bestId = json.optInt("best"),
                    updates = json.optInt("updates")
                )
            }
        }
        val root = JSONObject(text)
        val game = root.optJSONObject("_aGame")?.optInt("_idRow") ?: GAME_ID
        if (game != GAME_ID) throw GameBananaException("That GameBanana page isn't a Friday Night Funkin' mod.")
        val mod = parseMod(root) ?: throw GameBananaException("GameBanana didn't return a mod.")
        val files = root.optJSONArray("_aFiles")?.let { array -> (0 until array.length()).mapNotNull { array.optJSONObject(it)?.let(::parseFile) } }.orEmpty()
        val tags = root.optJSONArray("_aTags")?.let { array -> (0 until array.length()).joinToString(" ") { array.opt(it).toString() } }.orEmpty()
        val engine = engineOf(mod.name + " " + mod.category + " " + tags + " " + root.optJSONObject("_aSuperCategory")?.optString("_sName").orEmpty())
        return GameBananaDetails(mod, files, root.optInt("_nDownloadCount"), engine)
    }

    fun bestFile(files: List<GameBananaFile>): GameBananaFile? {
        if (files.any { it.role.isNotEmpty() }) {
            files.filter { it.score > -500 }.maxWithOrNull(compareBy<GameBananaFile> { it.score }.thenBy { it.added })?.let { return it }
        }
        return files.filter { it.clean }.sortedWith(compareByDescending<GameBananaFile> { it.isArchive }.thenByDescending { it.added }).firstOrNull()
            ?: files.maxByOrNull { it.added }
    }

    fun parseUpdates(text: String): List<GameBananaUpdate> {
        if (native) {
            nativeResult("updates", text)?.let { json ->
                return objects(json.optJSONArray("updates")).map { u ->
                    GameBananaUpdate(
                        u.optInt("id"), u.optString("name"), u.optString("version"), u.optLong("added"), u.optString("text"),
                        objects(u.optJSONArray("changes")).map { it.optString("category") to it.optString("text") },
                        u.optJSONArray("files")?.let { a -> (0 until a.length()).map { a.optInt(it) } }.orEmpty()
                    )
                }
            }
        }
        val records = JSONObject(text).optJSONArray("_aRecords") ?: return emptyList()
        return objects(records).map { r ->
            GameBananaUpdate(
                r.optInt("_idRow"), r.optString("_sName"), r.optString("_sVersion"), r.optLong("_tsDateAdded"),
                r.optString("_sText").replace(Regex("<[^>]+>"), " ").replace(Regex("\\s+"), " ").trim(),
                objects(r.optJSONArray("_aChangeLog")).map { it.optString("cat") to it.optString("text") },
                r.optJSONArray("_aFileRowIds")?.let { a -> (0 until a.length()).map { a.optInt(it) } }.orEmpty()
            )
        }
    }

    fun inspect(entries: List<String>): GameBananaInspection? {
        if (!native) return null
        val raw = VBananaNative.inspect(JSONArray().also { array -> entries.forEach { array.put(it) } }.toString()) ?: return null
        val json = JSONObject(raw)
        return GameBananaInspection(
            json.optString("engine").ifEmpty { null },
            json.optInt("confidence"),
            json.optString("root"),
            json.optBoolean("build"),
            strings(json.optJSONArray("modFolders")),
            strings(json.optJSONArray("songs")),
            json.optInt("entries"),
            json.optInt("lua"),
            json.optInt("hxc"),
            json.optInt("hx")
        )
    }

    private class Cached(val body: String, val mark: TimeSource.Monotonic.ValueTimeMark, val seconds: Int)

    private val cache = LinkedHashMap<String, Cached>()

    private fun cacheSeconds(url: String): Int = when {
        "/Subfeed" in url -> 300
        "/Search/" in url -> 120
        "/ProfilePage" in url || "/Updates" in url -> 600
        else -> 0
    }

    fun clearCache() {
        cache.clear()
    }

    private fun get(url: String): String {
        val seconds = cacheSeconds(url)
        cache[url]?.let { if (it.mark.elapsedNow().inWholeSeconds < it.seconds) return it.body else cache.remove(url) }
        val body = fetch(url)
        if (seconds > 0) {
            cache[url] = Cached(body, TimeSource.Monotonic.markNow(), seconds)
            while (cache.size > 64) cache.remove(cache.keys.first())
        }
        return body
    }

    private fun nativeUrl(request: JSONObject): String? {
        val raw = VBananaNative.request(request.toString()) ?: return null
        val json = JSONObject(raw)
        json.optString("error").takeIf { it.isNotEmpty() }?.let { throw GameBananaException(it) }
        return json.optString("url").ifEmpty { null }
    }

    private fun fetch(url: String): String {
        val response = try {
            Http.request(url, "GET", headers, null, TIMEOUT_MS)
        } catch (e: Exception) {
            throw GameBananaException("Couldn't reach GameBanana: ${e.message ?: e::class.simpleName}")
        }
        if (response.status == 404) throw GameBananaException("GameBanana has no such mod.")
        if (response.status !in 200..299) throw GameBananaException("GameBanana answered ${response.status}.")
        return response.body.decodeToString()
    }

    fun feedUrl(feed: GameBananaFeed, page: Int): String =
        (if (native) nativeUrl(JSONObject().put("kind", "feed").put("sort", feed.sort).put("page", page)) else null) ?: "$API/Game/$GAME_ID/Subfeed?_nPage=$page&_sSort=${feed.sort}&_csvModelInclusions=Mod"

    fun searchUrl(query: String, page: Int): String =
        (if (native) nativeUrl(JSONObject().put("kind", "search").put("query", query).put("page", page)) else null) ?: "$API/Util/Search/Results?_sModelName=Mod&_sOrder=best_match&_idGameRow=$GAME_ID&_sSearchString=${encode(query.trim())}&_nPage=$page"

    fun detailsUrl(id: Int): String = "$API/Mod/$id/ProfilePage"

    fun updatesUrl(id: Int, page: Int = 1, perPage: Int = 10): String = "$API/Mod/$id/Updates?_nPage=$page&_nPerpage=$perPage"

    fun updates(id: Int, page: Int = 1): List<GameBananaUpdate> = parseUpdates(get(updatesUrl(id, page)))

    fun feed(feed: GameBananaFeed, page: Int = 1): GameBananaPage = parsePage(get(feedUrl(feed, page)))

    fun search(query: String, page: Int = 1): GameBananaPage {
        parseModId(query)?.let { id -> return GameBananaPage(listOf(details(id).mod), 1, true) }
        return try {
            parsePage(get(searchUrl(query, page)))
        } catch (e: GameBananaException) {
            val words = query.lowercase().split(' ').filter { it.isNotBlank() }
            val found = (1..3).flatMap { feed(GameBananaFeed.POPULAR, it).mods }.filter { mod -> words.all { mod.name.lowercase().contains(it) } }
            GameBananaPage(found, found.size, true)
        }
    }

    fun details(id: Int): GameBananaDetails = parseDetails(get(detailsUrl(id)))

    fun safeFileName(file: GameBananaFile): String =
        file.name.replace(Regex("[^A-Za-z0-9._ -]"), "_").trim().ifEmpty { "mod-${file.id}.zip" }

    fun download(file: GameBananaFile, onProgress: (Long, Long) -> Unit = { _, _ -> }): ByteArray {
        val response = try {
            Http.request(file.url, "GET", mapOf("User-Agent" to "V-Converter"), null, TIMEOUT_MS * 4, onProgress)
        } catch (e: Exception) {
            throw GameBananaException("The download failed: ${e.message ?: e::class.simpleName}")
        }
        if (response.status !in 200..299) throw GameBananaException("The download failed (${response.status}).")
        return response.body
    }
}
