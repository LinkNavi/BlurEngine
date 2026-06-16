#!/usr/bin/env python3
import os
import sys

EXTS = {
    ".c",
    ".h",
    ".cpp",
    ".hpp",
    ".cc",
    ".cxx",
    ".hxx",
    ".frag",
    ".vert",
    ".geom",
    ".comp",
    ".glsl",
    ".py",
    ".js",
    ".ts",
    ".rs",
    ".go",
    ".java",
    ".cs",
    ".lua",
    ".sh",
    ".asm",
    ".s",
}


def count_lines(path):
    n = 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            n += chunk.count(b"\n")
    return n


def norm(p):
    return p.replace("\\", "/").strip("/")


def is_excluded(rel_dir_path, dirname, excludes):
    rel_full = norm(os.path.join(rel_dir_path, dirname))
    for ex in excludes:
        ex = norm(ex)
        if "/" in ex:
            # path-style exclude: match exact relative path or anything under it
            if rel_full == ex or rel_full.startswith(ex + "/"):
                return True
        else:
            # bare-name exclude: match this directory by name anywhere
            if dirname == ex:
                return True
    return False


def main():
    if len(sys.argv) < 2:
        print("usage: loc_counter.py <folder> [--exclude name1,name2,...]")
        return

    root = sys.argv[1]
    excludes = set()
    if "--exclude" in sys.argv:
        i = sys.argv.index("--exclude")
        if i + 1 < len(sys.argv):
            excludes = set(sys.argv[i + 1].split(","))

    total = 0
    per_ext = {}

    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = norm(os.path.relpath(dirpath, root))
        if rel_dir == ".":
            rel_dir = ""

        dirnames[:] = [d for d in dirnames if not is_excluded(rel_dir, d, excludes)]

        for fn in filenames:
            ext = os.path.splitext(fn)[1].lower()
            if ext in EXTS:
                path = os.path.join(dirpath, fn)
                try:
                    lines = count_lines(path)
                except OSError:
                    continue
                total += lines
                per_ext[ext] = per_ext.get(ext, 0) + lines

    for ext, lines in sorted(per_ext.items(), key=lambda x: -x[1]):
        print(f"{ext:8} {lines}")
    print(f"{'total':8} {total}")


if __name__ == "__main__":
    main()
