package com.vconvertercrew.vconverter.views.overlay

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.vconvertercrew.vtouch.vMenuNavigation
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

data class EngineChoice(val key: String, val label: String, val title: String)

object EngineChoices {
    val CNE = EngineChoice("codename", "CNE", "Codename Engine")
    val FPS = EngineChoice("fpsplus", "FPS", "FPS Plus")
    val LUA = EngineChoice("luaslice", "LUA SLICE", "LuaSlice")
    val NV = EngineChoice("nmv", "NV", "NightmareVision")
    val PSYCH = EngineChoice("psych", "PSYCH", "Psych Engine")
    val VSLICE = EngineChoice("vslice", "V SLICE", "V-Slice")

    val left = listOf(CNE, FPS, LUA, NV, PSYCH, VSLICE)
    val right = listOf(VSLICE, PSYCH, NV, LUA, FPS, CNE)
}

@Composable
fun FunkinEnginePicker(
    title: String,
    art: MenuArt,
    available: (String, String) -> Boolean,
    options: (EngineChoice, EngineChoice) -> List<MenuOption>,
    onClose: () -> Unit,
    modifier: Modifier = Modifier,
    closeRequests: Int = 0,
    overlay: @Composable BoxScope.() -> Unit = {}
) {
    var picked by remember { mutableStateOf<Pair<EngineChoice, EngineChoice>?>(null) }
    var innerClose by remember { mutableIntStateOf(0) }
    LaunchedEffect(closeRequests) {
        if (closeRequests > 0 && picked != null) innerClose++
    }
    picked?.let { (from, to) ->
        FunkinOptionsMenu(
            title = "${from.label} to ${to.label}",
            options = options(from, to),
            art = art,
            onClose = { picked = null },
            modifier = modifier,
            closeRequests = innerClose,
            overlay = overlay
        )
        return
    }
    EngineColumns(title, art, available, { from, to ->
        innerClose = 0
        picked = from to to
    }, onClose, modifier, closeRequests, overlay)
}

@Composable
private fun EngineColumns(
    title: String,
    art: MenuArt,
    available: (String, String) -> Boolean,
    onPick: (EngineChoice, EngineChoice) -> Unit,
    onClose: () -> Unit,
    modifier: Modifier,
    closeRequests: Int,
    overlay: @Composable BoxScope.() -> Unit
) {
    val left = EngineChoices.left
    val right = EngineChoices.right
    var column by remember { mutableIntStateOf(0) }
    var from by remember { mutableIntStateOf(left.indexOf(EngineChoices.PSYCH)) }
    var to by remember { mutableIntStateOf(right.indexOf(EngineChoices.VSLICE)) }
    var closing by remember { mutableStateOf(false) }
    var backFrame by remember { mutableIntStateOf(0) }
    val closeAction = rememberUpdatedState(onClose)
    val pick = rememberUpdatedState(onPick)
    val scope = rememberCoroutineScope()
    val focus = remember { FocusRequester() }
    val leftPosition by animateFloatAsState(from.toFloat(), spring(dampingRatio = 0.85f, stiffness = 260f), label = "left")
    val rightPosition by animateFloatAsState(to.toFloat(), spring(dampingRatio = 0.85f, stiffness = 260f), label = "right")
    val fade by animateFloatAsState(if (closing) 0f else 1f, tween(180), label = "fade")
    val ticker = rememberInfiniteTransition(label = "alphabet")
    val tickValue by ticker.animateFloat(0f, 4f, infiniteRepeatable(tween(4 * 1000 / 24, easing = LinearEasing), RepeatMode.Restart), label = "tick")
    val tick = tickValue.toInt().coerceIn(0, 3)
    val leftRects = remember { arrayOf<List<Rect>>(emptyList()) }
    val rightRects = remember { arrayOf<List<Rect>>(emptyList()) }
    val backRect = remember { arrayOf(Rect.Zero) }
    val area = remember { floatArrayOf(1280f, 720f) }

    fun pairOk(f: Int, t: Int): Boolean = left[f].key != right[t].key && available(left[f].key, right[t].key)
    fun sourceOk(f: Int): Boolean = right.indices.any { pairOk(f, it) }

    fun close() {
        if (closing) return
        closing = true
        UiSound.click()
        scope.launch {
            art.back?.takeIf { it.skin.playOnPress }?.let { back ->
                for (i in back.frames.indices) {
                    backFrame = i
                    delay(back.skin.frameMillis)
                }
            }
            FunkinTransition.go { closeAction.value() }
        }
    }

    fun move(change: Int) {
        if (change == 0 || closing) return
        if (column == 0) from = ((from + change) % left.size + left.size) % left.size
        else to = ((to + change) % right.size + right.size) % right.size
        UiSound.click()
    }

    fun switchTo(target: Int) {
        if (target == column || closing) return
        column = target
        if (target == 1 && !pairOk(from, to)) right.indices.firstOrNull { pairOk(from, it) }?.let { to = it }
        UiSound.click()
    }

    fun accept() {
        if (closing) return
        if (column == 0) {
            if (sourceOk(from)) switchTo(1)
            return
        }
        if (!pairOk(from, to)) return
        UiSound.click()
        pick.value(left[from], right[to])
    }

    fun back() {
        if (column == 1) switchTo(0) else close()
    }

    var seenClose by remember { mutableIntStateOf(closeRequests) }
    LaunchedEffect(closeRequests) {
        if (closeRequests > seenClose) {
            seenClose = closeRequests
            back()
        }
    }

    LaunchedEffect(Unit) {
        runCatching { focus.requestFocus() }
    }

    Box(
        modifier
            .fillMaxSize()
            .background(Color.Black)
            .focusRequester(focus)
            .focusable()
            .onKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) return@onKeyEvent false
                when (event.key) {
                    Key.DirectionUp, Key.W -> move(-1)
                    Key.DirectionDown, Key.S -> move(1)
                    Key.DirectionLeft, Key.A -> switchTo(0)
                    Key.DirectionRight, Key.D -> if (sourceOk(from)) switchTo(1)
                    Key.Enter, Key.NumPadEnter, Key.Spacebar -> accept()
                    Key.Escape, Key.Backspace, Key.Back -> back()
                    else -> return@onKeyEvent false
                }
                true
            }
            .onSizeChanged {
                area[0] = it.width.toFloat()
                area[1] = it.height.toFloat()
            }
            .vMenuNavigation(
                key = Unit,
                step = { 120f * 1.3f * pickerScale(area[0], area[1]) },
                onMove = { move(it) },
                onTap = { tap ->
                    when {
                        backRect[0].contains(tap) -> back()
                        else -> {
                            val l = leftRects[0].indexOfFirst { it.contains(tap) }
                            val r = rightRects[0].indexOfFirst { it.contains(tap) }
                            if (l >= 0) {
                                if (column == 0 && l == from) accept() else {
                                    column = 0
                                    from = l
                                    UiSound.click()
                                }
                            } else if (r >= 0 && sourceOk(from)) {
                                if (column == 1 && r == to) accept() else {
                                    column = 1
                                    to = r
                                    UiSound.click()
                                }
                            }
                        }
                    }
                },
                onBack = { back() }
            )
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val w = size.width
            val h = size.height
            art.background?.let { bg ->
                val cover = max(w / bg.width, h / bg.height) * 1.1f
                val dw = bg.width * cover
                val dh = bg.height * cover
                drawImage(bg, dstOffset = IntOffset(((w - dw) / 2).roundToInt(), ((h - dh) / 2).roundToInt()), dstSize = IntSize(dw.roundToInt(), dh.roundToInt()), alpha = fade)
            }
            val scale = pickerScale(w, h)
            val font = art.font
            val lineHeight = (font?.lineHeight ?: 70f) * scale
            fun width(text: String) = (font?.width(text) ?: text.length * 45f) * scale

            val leftHits = ArrayList<Rect>(left.size)
            left.forEachIndexed { index, engine ->
                val target = index - leftPosition
                val x = (70f + target * 20f) * scale
                val y = h * 0.48f + target * 1.3f * 120f * scale
                val text = engine.label
                val active = column == 0
                val base = when {
                    !sourceOk(index) -> 0.2f
                    index == from -> if (active) 1f else 0.85f
                    else -> if (active) 0.6f else 0.3f
                }
                leftHits += Rect(0f, y - 10f * scale, w / 2f, y + lineHeight + 10f * scale)
                if (y + lineHeight < 0 || y > h) return@forEachIndexed
                if (font != null) with(font) { draw(text, x, y, scale, base * fade, tick) }
            }
            leftRects[0] = leftHits

            val rightHits = ArrayList<Rect>(right.size)
            right.forEachIndexed { index, engine ->
                val target = index - rightPosition
                val text = engine.label
                val x = w - (70f + target * 20f) * scale - width(text)
                val y = h * 0.48f + target * 1.3f * 120f * scale
                val active = column == 1
                val ok = pairOk(from, index)
                val base = when {
                    !ok -> 0.15f
                    index == to -> if (active) 1f else 0.7f
                    else -> if (active) 0.6f else 0.3f
                }
                rightHits += Rect(w / 2f, y - 10f * scale, w, y + lineHeight + 10f * scale)
                if (y + lineHeight < 0 || y > h) return@forEachIndexed
                if (font != null) with(font) { draw(text, x, y, scale, base * fade, tick) }
            }
            rightRects[0] = rightHits

            val arrow = "TO"
            val arrowScale = scale * 0.7f
            val arrowWidth = (font?.width(arrow) ?: arrow.length * 45f) * arrowScale
            if (font != null) with(font) {
                draw(arrow, (w - arrowWidth) / 2f, h * 0.48f + (lineHeight - font.lineHeight * arrowScale) / 2f, arrowScale, (if (column == 1) 1f else 0.5f) * fade, tick)
            }
            backRect[0] = art.back?.let { back -> with(back) { draw(backFrame, scale, fade) } } ?: Rect.Zero
        }
        if (art.font == null) {
            Row(Modifier.fillMaxWidth().align(Alignment.Center).padding(horizontal = 32.dp)) {
                Text(left[from].label, color = Color.Black, fontSize = 32.sp, fontWeight = FontWeight.Black, modifier = Modifier.weight(1f))
                Text("TO", color = Color.Black.copy(alpha = 0.6f), fontSize = 24.sp, fontWeight = FontWeight.Black)
                Text(right[to].label, color = Color.Black, fontSize = 32.sp, fontWeight = FontWeight.Black, textAlign = TextAlign.End, modifier = Modifier.weight(1f))
            }
        }
        Text(
            title.uppercase(),
            color = Color.White,
            fontSize = 14.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.align(Alignment.TopStart).padding(16.dp).clip(RoundedCornerShape(8.dp)).background(Color(0x66000000)).padding(horizontal = 10.dp, vertical = 4.dp)
        )
        overlay()
        val hint = when {
            column == 0 && !sourceOk(from) -> "${left[from].title} can't be converted from yet."
            column == 0 -> "Convert from ${left[from].title}. Press right or Enter to pick where it goes."
            pairOk(from, to) -> "${left[from].title} to ${right[to].title}. Press Enter to choose what to convert."
            left[from].key == right[to].key -> "Pick a different engine on the right."
            else -> "${left[from].title} to ${right[to].title} isn't supported yet."
        }
        Text(
            hint,
            color = Color.White,
            fontSize = 13.sp,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(start = 16.dp, end = 120.dp, bottom = 16.dp)
                .widthIn(max = 640.dp)
                .clip(RoundedCornerShape(10.dp))
                .background(Color(0x99000000))
                .padding(horizontal = 14.dp, vertical = 8.dp)
        )
    }
}

private fun pickerScale(width: Float, height: Float): Float = min(width / 1500f, height / 720f).coerceAtLeast(0.2f)
