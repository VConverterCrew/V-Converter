package com.vconvertercrew.vconverter.views.overlay

data class Credit(val name: String, val role: String, val url: String)

object Credits {
    val people = listOf(
        Credit("Brenninho", "Made V-Converter.", "https://github.com/Brenninho123"),
        Credit("FLA_Max", "Had the idea for the Touch Cursor and created LuaSlice.", "https://github.com/Fla-man-Max")
    )

    fun submenu(open: (String) -> Unit): MenuOption = MenuOption.Submenu("Credits", "The people behind V-Converter and the ideas in it.") {
        people.map { person -> MenuOption.Action(person.name, "${person.role} Enter opens their GitHub profile.") { open(person.url) } }
    }
}
