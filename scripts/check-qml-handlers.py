#!/usr/bin/env python3
"""Checks every signal handler in the app's QML (`onTriggered:`, `onMoveEnded:`, `input.onSubmitted:`)
against the SDK's type information: the signal must exist on the element the handler sits in.

Why: Qt 4.8 refuses to load a document with a handler for a signal that does not exist ("Cannot
assign to non-existent property"), which for a file loaded at start means the app quits with
nothing on screen, and cascades-qmllint.sh does not look at handler names (tried 2026-11-14).

    python3 scripts/check-qml-handlers.py            # app/assets/*.qml; exit 1 if anything is wrong

The type information is the cache cascades-qmllint.sh makes (~/.cache/bb10-qmllint/imports).
It reads QML by its layout (one element or property per line, as the files are written), not
with a parser: a line it cannot place is reported rather than passed.
"""
import glob, os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
IMPORTS = os.path.expanduser("~/.cache/bb10-qmllint/imports/bb")
# Types registered from C++ (app/src/main.cpp): QTimer as Timer.
EXTRA = {"Timer": {"signals": {"timeout"}, "props": {"interval": "", "singleShot": ""}}}


def load_types():
    """name -> {proto, signals, props}; and QML name -> C++ name."""
    comps, exports = {}, {}
    for path in glob.glob(IMPORTS + "/**/plugins.qmltypes", recursive=True):
        text = open(path).read()
        for block in re.split(r"\n    Component \{\n", text)[1:]:
            name = re.search(r'name: "([^"]+)"', block).group(1)
            proto = re.search(r'\n        prototype: "([^"]+)"', block)
            c = comps.setdefault(name, {"proto": proto.group(1) if proto else None, "signals": set(), "props": {}})
            for m in re.finditer(r'\n        Signal \{\s*name: "(\w+)"', block):
                c["signals"].add(m.group(1))
            for m in re.finditer(r'\n        Property \{(.*?)\}', block, re.S):
                n = re.search(r'name: "(\w+)"', m.group(1))
                t = re.search(r'type: "([^"]+)"', m.group(1))
                if n:
                    c["props"][n.group(1)] = t.group(1) if t else ""
            listed = re.search(r'\n        exports: \[(.*?)\]', block, re.S)
            for m in re.finditer(r'"[\w.]+/(\w+) [\d.]+"', listed.group(1) if listed else ""):
                # The QML-side subclass (QmlListView, QmlTab) wins the name over its base.
                if m.group(1) not in exports or "::Qml" in name:
                    exports[m.group(1)] = name
    return comps, exports


COMPS, EXPORTS = load_types()


def chain(cpp):
    while cpp and cpp in COMPS:
        yield COMPS[cpp]
        cpp = COMPS[cpp]["proto"]


def has_signal(cpp, signal):
    return any(signal in c["signals"] for c in chain(cpp))


def prop_type(cpp, prop):
    for c in chain(cpp):
        if prop in c["props"]:
            return c["props"][prop]
    return None


class Doc:
    """One QML file: its root element's type and what it declares itself."""

    def __init__(self, path):
        self.path, self.lines = path, open(path).read().split("\n")
        self.root, self.declared_signals, self.declared_props = None, set(), set()
        depth = 0
        for line in self.lines:
            s = line.strip()
            if self.root is None:
                m = re.match(r"([A-Z]\w*)\s*\{", s)
                if m:
                    self.root = m.group(1)
            elif depth == 1:
                m = re.match(r"(?:default\s+)?property\s+\S+\s+(\w+)", s)
                if m:
                    self.declared_props.add(m.group(1))
                m = re.match(r"signal\s+(\w+)", s)
                if m:
                    self.declared_signals.add(m.group(1))
            depth += s.count("{") - s.count("}")


DOCS = {os.path.basename(p)[:-4]: Doc(p) for p in sorted(glob.glob(os.path.join(ROOT, "app", "assets", "*.qml")))}


def resolve(qml_name, seen=()):
    """A QML element name -> (C++ type or None, declared signals, declared props)."""
    if qml_name in DOCS and qml_name not in seen:
        d = DOCS[qml_name]
        cpp, sig, props = resolve(d.root, seen + (qml_name,))
        return cpp, sig | d.declared_signals, props | d.declared_props
    if qml_name in EXTRA:
        return "extra:" + qml_name, set(EXTRA[qml_name]["signals"]), set(EXTRA[qml_name]["props"])
    return EXPORTS.get(qml_name), set(), set()


def check(doc):
    problems, stack = [], []          # stack entries: ("js",) or ("el", cpp, signals, props, label)
    root_seen = False
    doc.checked = 0
    for no, line in enumerate(doc.lines, 1):
        s = line.split("//")[0].strip() if '"' not in line else line.strip()
        top = stack[-1] if stack else None
        in_element = top is None or top[0] == "el"
        handler = re.match(r"((?:[a-z]\w*\.)*)on([A-Z]\w*)\s*:", s) if in_element and top else None
        if in_element and top:                    # what the element declares itself: onXChanged, onX
            m = re.match(r"(?:default\s+)?property\s+\S+\s+(\w+)", s)
            if m:
                top[3].add(m.group(1))
            m = re.match(r"signal\s+(\w+)", s)
            if m:
                top[2].add(m.group(1))
        if handler:
            doc.checked += 1
            cpp, sigs, props, label = top[1], top[2], top[3], top[4]
            for part in [p for p in handler.group(1).split(".") if p]:      # input.onSubmitted
                t = prop_type(cpp, part) if cpp else None
                if t is None:
                    problems.append((no, "%s has no property '%s' (in '%s')" % (label, part, s)))
                    cpp = None
                    break
                cpp, sigs, props, label = t, set(), set(), label + "." + part
            name = handler.group(2)
            signal = name[0].lower() + name[1:]
            ok = cpp is not None and (signal in sigs or (cpp.startswith("extra:") is False and has_signal(cpp, signal)))
            if not ok and signal.endswith("Changed"):        # a property declared in QML: onXChanged
                base = signal[:-7]
                ok = base in props
            if not ok and cpp is not None:
                problems.append((no, "%s has no signal '%s' (handler %s)" % (label, signal, handler.group(0).rstrip(":"))))
        # what this line opens and closes
        for ch_i, ch in enumerate(s):
            if ch == "{":
                before = s[:ch_i].strip()
                kind = ("js",)
                if in_element and ch_i == s.index("{"):
                    m = re.match(r"(?:[\w.]+\s*:\s*)?([A-Z]\w*)$", before)          # Type {   |   prop: Type {
                    g = re.match(r"([a-z]\w*)$", before)                            # grouped {
                    if m:
                        cpp, sigs, props = resolve(m.group(1))
                        if cpp is None and m.group(1) not in DOCS:
                            problems.append((no, "unknown element type '%s'" % m.group(1)))
                        if not root_seen:         # the document's root: all it declares, wherever
                            root_seen = True
                            sigs, props = sigs | doc.declared_signals, props | doc.declared_props
                        kind = ("el", cpp, sigs, props, m.group(1))
                    elif g and top and top[0] == "el":
                        t = prop_type(top[1], g.group(1)) if top[1] else None
                        if t is None:
                            problems.append((no, "%s has no property '%s' to group" % (top[4], g.group(1))))
                        kind = ("el", t, set(), set(), top[4] + "." + g.group(1))
                stack.append(kind)
                top = stack[-1]
                in_element = top[0] == "el"
            elif ch == "}":
                if stack:
                    stack.pop()
                top = stack[-1] if stack else None
                in_element = top is None or top[0] == "el"
    if stack:
        problems.append((len(doc.lines), "unbalanced braces: could not follow the file's layout"))
    return problems


def main():
    if not COMPS:
        sys.exit("no type information under %s: run cascades-qmllint.sh once" % IMPORTS)
    bad = handlers = 0
    for name, doc in DOCS.items():
        written = sum(1 for l in doc.lines if re.match(r"\s*(?:[a-z]\w*\.)*on[A-Z]\w*\s*:", l))
        handlers += written
        problems = check(doc)
        if doc.checked != written:    # a handler the layout reader walked past is not a pass
            problems.append((0, "%d handlers written, %d placed in an element" % (written, doc.checked)))
        for no, what in problems:
            bad += 1
            print("%s:%d: %s" % (os.path.relpath(doc.path, ROOT), no, what))
    print("%d documents, %d handlers, %d problems" % (len(DOCS), handlers, bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
        # The check must find what it is for: a handler that does not exist, three ways.
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".qml", delete=False) as f:
            f.write('import bb.cascades 1.4\nPage {\n    id: page\n    property int n: 0\n    onNChanged: {\n    }\n'
                    '    onBogus: {\n    }\n    Container {\n        ListView {\n            rearrangeHandler {\n'
                    '                onMoveEnded: {\n                }\n                onMoveBogus: {\n                }\n'
                    '            }\n            onTriggered: {\n                if (true) {\n                }\n            }\n'
                    '        }\n        TextField {\n            input.onSubmitted: {\n            }\n'
                    '            input.onBogus: {\n            }\n        }\n    }\n}\n')
        found = [w for _, w in check(Doc(f.name))]
        os.unlink(f.name)
        assert len(found) == 3 and all("ogus" in w for w in found), found
        print("self-test: ok")
    else:
        main()
