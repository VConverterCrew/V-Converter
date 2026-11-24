import os
import re
import sys


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def walk(root, ext=".hx"):
    for base, _, files in os.walk(root):
        for name in sorted(files):
            if name.endswith(ext):
                yield os.path.join(base, name)


def split_params(text):
    out, depth, cur = [], 0, ""
    for c in text:
        if c in "(<[{":
            depth += 1
        elif c in ")>]}":
            depth -= 1
        if c == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += c
    if cur.strip():
        out.append(cur)
    return [p.strip() for p in out]


def closing(text, start):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    return -1


def psych(root):
    functions, deprecated, callbacks, globals_ = {}, {}, set(), set()
    pattern = re.compile(r'(?:addLocalCallback|add_callback)\((?:lua, )?"([A-Za-z_][A-Za-z0-9_]*)",\s*function\s*\(')
    for path in walk(root):
        text = read(path)
        for m in pattern.finditer(text):
            name = m.group(1)
            open_at = m.end() - 1
            close_at = closing(text, open_at)
            params = split_params(text[open_at + 1:close_at])
            names, required = [], 0
            for p in params:
                optional = p.startswith("?") or "=" in p
                pname = p.lstrip("?").split(":")[0].split("=")[0].strip()
                names.append(pname)
                if not optional:
                    required = len(names)
            if name not in functions or len(names) > functions[name][1]:
                functions[name] = (required, len(names), ",".join(names))
            body = text[close_at:close_at + 400]
            dep = re.search(r'luaTrace\("([A-Za-z_]+) is deprecated! Use ([A-Za-z_]+)', body)
            if dep and dep.group(1) == name:
                deprecated[name] = dep.group(2)
        for m in re.finditer(r"(?:callOnLuas|callOnScripts|callOnHScript|callOnAllHS)\('([A-Za-z_]+)'", text):
            callbacks.add(m.group(1))
        for m in re.finditer(r"\bcall\('([A-Za-z_]+)'", text):
            callbacks.add(m.group(1))
        if path.endswith("FunkinLua.hx"):
            for m in re.finditer(r"\bset\('([A-Za-z_][A-Za-z0-9_]*)'", text):
                globals_.add(m.group(1))
    callbacks.update(["onCreate", "onCreatePost", "onDestroy", "onTimerCompleted", "onTweenCompleted", "onSoundFinished"])
    return functions, deprecated, sorted(callbacks), sorted(globals_)


def haxe_types(roots):
    types = {}
    decl = re.compile(r"^\s*(?:@:[^\n]*\n\s*)*(?:@:[A-Za-z]+(?:\([^)\n]*\))?\s+)*(?:(?:private|public|final|extern|abstract)\s+)*(class|interface|enum|abstract|typedef)\s+([A-Z][A-Za-z0-9_]*)", re.M)
    for root, prefer in roots:
        for path in walk(root):
            text = read(path)
            pkg = re.search(r"^\s*package\s+([a-zA-Z0-9_.]*)\s*;", text, re.M)
            package = pkg.group(1) if pkg else ""
            for m in decl.finditer(text):
                before = text[max(0, m.start() - 40):m.start()]
                if "private" in text[m.start():m.end()].split(m.group(1))[0]:
                    continue
                kind, name = m.group(1), m.group(2)
                if kind == "enum" and text[m.end():m.end() + 10].lstrip().startswith("abstract"):
                    kind = "abstract"
                full = (package + "." if package else "") + name
                types.setdefault(name, []).append((full, kind, prefer))
    return types


def vslice(root):
    events = {}
    text = read(os.path.join(root, "funkin/modding/IScriptedClass.hx"))
    for m in re.finditer(r"public function (on[A-Za-z]+)\(event:([A-Za-z]+)\)", text):
        events[m.group(1)] = m.group(2)
    interfaces = {}
    for m in re.finditer(r"interface\s+([A-Za-z]+)((?:\s+extends\s+[A-Za-z]+)*)\s*\{(.*?)\n\}", text, re.S):
        parents = re.findall(r"extends\s+([A-Za-z]+)", m.group(2))
        methods = re.findall(r"function (on[A-Za-z]+)\(", m.group(3))
        interfaces[m.group(1)] = (parents, methods)
    classes = {}
    decl = re.compile(r"^\s*(?:@:[^\n]*\n\s*)*(?:(?:public|final|abstract)\s+)*class\s+([A-Z][A-Za-z0-9_]*)(?:<[^>{]*>)?([^{]*)\{", re.M)
    for path in walk(root):
        for m in decl.finditer(read(path)):
            rest = m.group(2)
            parent = re.search(r"extends\s+([A-Za-z0-9_.]+)", rest)
            impl = re.findall(r"implements\s+([A-Za-z0-9_.]+)", rest)
            classes.setdefault(m.group(1), (parent.group(1).split(".")[-1] if parent else None, [i.split(".")[-1] for i in impl]))

    def iface(name, seen):
        if name not in interfaces or name in seen:
            return set()
        seen.add(name)
        parents, methods = interfaces[name]
        out = set(methods)
        for p in parents:
            out |= iface(p, seen)
        return out

    def calls(name, depth=0):
        if name not in classes or depth > 20:
            return set()
        parent, impl = classes[name]
        out = set()
        for i in impl:
            out |= iface(i, set())
        if parent:
            out |= calls(parent, depth + 1)
        return out

    bases = {}
    for name in classes:
        c = calls(name)
        if c:
            bases[name] = sorted(c)
    return events, bases


def psych_classes(root):
    out = {}
    for path in walk(root):
        text = read(path)
        pkg = re.search(r"^\s*package\s+([a-zA-Z0-9_.]*)\s*;", text, re.M)
        package = pkg.group(1) if pkg else ""
        for m in re.finditer(r"^\s*(?:@:[^\n]*\n\s*)*(?:(?:public|final)\s+)*(?:class|enum|abstract)\s+([A-Z][A-Za-z0-9_]*)", text, re.M):
            if package:
                out.setdefault(m.group(1), package + "." + m.group(1))
    return out


def best(entries):
    def score(entry):
        full, kind, prefer = entry
        return (kind == "typedef", -prefer, full.count("."), len(full))
    return sorted(entries, key=score)[0]


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    psych_root, funkin_root, flixel_root, addons_root, out = sys.argv[1:6]
    functions, deprecated, callbacks, globals_ = psych(psych_root)
    types = haxe_types([(funkin_root, 3), (flixel_root, 2), (addons_root, 1)])
    events, bases = vslice(funkin_root)
    pclasses = psych_classes(psych_root)
    lines = ['#include "ApiData.hpp"', "", "namespace vscript {", ""]
    lines.append("const LuaApiFunction kPsychFunctions[] = {")
    for name in sorted(functions):
        req, total, names = functions[name]
        lines.append(f"    {{{cstr(name)}, {req}, {total}, {cstr(names)}}},")
    lines.append("};")
    lines.append("const size_t kPsychFunctionCount = sizeof(kPsychFunctions) / sizeof(kPsychFunctions[0]);")
    lines.append("")
    lines.append("const NamePair kPsychDeprecated[] = {")
    for name in sorted(deprecated):
        lines.append(f"    {{{cstr(name)}, {cstr(deprecated[name])}}},")
    lines.append("};")
    lines.append("const size_t kPsychDeprecatedCount = sizeof(kPsychDeprecated) / sizeof(kPsychDeprecated[0]);")
    lines.append("")
    lines.append("const char* const kPsychCallbacks[] = {")
    lines.extend(f"    {cstr(c)}," for c in callbacks)
    lines.append("};")
    lines.append("const size_t kPsychCallbackCount = sizeof(kPsychCallbacks) / sizeof(kPsychCallbacks[0]);")
    lines.append("")
    lines.append("const char* const kPsychGlobals[] = {")
    lines.extend(f"    {cstr(g)}," for g in globals_)
    lines.append("};")
    lines.append("const size_t kPsychGlobalCount = sizeof(kPsychGlobals) / sizeof(kPsychGlobals[0]);")
    lines.append("")
    lines.append("const HaxeType kHaxeTypes[] = {")
    for name in sorted(types):
        full, kind, _ = best(types[name])
        lines.append(f"    {{{cstr(name)}, {cstr(full)}, {cstr(kind)}}},")
    lines.append("};")
    lines.append("const size_t kHaxeTypeCount = sizeof(kHaxeTypes) / sizeof(kHaxeTypes[0]);")
    lines.append("")
    lines.append("const NamePair kVSliceCallbacks[] = {")
    for name in sorted(events):
        lines.append(f"    {{{cstr(name)}, {cstr(events[name])}}},")
    lines.append("};")
    lines.append("const size_t kVSliceCallbackCount = sizeof(kVSliceCallbacks) / sizeof(kVSliceCallbacks[0]);")
    lines.append("")
    lines.append("const NamePair kScriptBases[] = {")
    for name in sorted(bases):
        lines.append(f"    {{{cstr(name)}, {cstr(','.join(bases[name]))}}},")
    lines.append("};")
    lines.append("const size_t kScriptBaseCount = sizeof(kScriptBases) / sizeof(kScriptBases[0]);")
    lines.append("")
    paths = sorted({full for entries in types.values() for full, kind, _ in entries if kind != "typedef"})
    lines.append("const char* const kHaxePaths[] = {")
    lines.extend(f"    {cstr(p)}," for p in paths)
    lines.append("};")
    lines.append("const size_t kHaxePathCount = sizeof(kHaxePaths) / sizeof(kHaxePaths[0]);")
    lines.append("")
    lines.append("const NamePair kPsychClasses[] = {")
    for name in sorted(pclasses):
        lines.append(f"    {{{cstr(name)}, {cstr(pclasses[name])}}},")
    lines.append("};")
    lines.append("const size_t kPsychClassCount = sizeof(kPsychClasses) / sizeof(kPsychClasses[0]);")
    lines.append("")
    lines.append("}")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(len(functions), "functions,", len(deprecated), "deprecated,", len(callbacks), "callbacks,", len(globals_), "globals,", len(types), "types,", len(events), "events,", len(bases), "bases,", len(paths), "paths,", len(pclasses), "psych classes")


main()
