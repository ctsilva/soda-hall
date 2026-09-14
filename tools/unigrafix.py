#!/usr/bin/env python3
"""Read the flat UniGrafix files of the U.C. Berkeley Soda Hall WALKTHRU Model.

UniGrafix is the Berkeley scene description language of the 1980s and 1990s. The two
Soda Hall files use only its flat subset: named vertices, polygon faces referring to those
names, and named colors. Comments are brace-delimited and nest. Statements end in
semicolons:

    { csb5: 2,783 vertices; 1,685 faces }
    c_rgb rd 0.785 0.785 0.712 ;
    v Be 1636 1561 716 ;
    f df (Be Ce De Ee) rd ;

No macros, instances, or multi-contour faces appear in these files, and this reader does
not implement them; it raises on any other statement kind. The result is a `vrml1.Mesh`
in the file's own coordinates, which for these files is the same building frame as the
VRML model (inches, Z up).
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vrml1  # noqa: E402

DEFAULT_COLOR = (0.8, 0.8, 0.8)


class UniGrafixError(Exception):
    pass


def strip_comments(text):
    """Removes brace-delimited comments, which may nest."""
    out = []
    depth = 0
    for char in text:
        if char == "{":
            depth += 1
        elif char == "}":
            if depth == 0:
                raise UniGrafixError("unbalanced '}' in comment")
            depth -= 1
        elif depth == 0:
            out.append(char)
    if depth != 0:
        raise UniGrafixError("unterminated comment")
    return "".join(out)


def statements(text):
    for statement in strip_comments(text).split(";"):
        words = statement.replace("(", " ( ").replace(")", " ) ").split()
        if words:
            yield words


def flatten(text, source=""):
    """Parses UniGrafix text into a Mesh with one color per face."""
    vertices = {}
    colors = {}
    mesh = vrml1.Mesh()
    for words in statements(text):
        kind = words[0]
        if kind == "v":
            if len(words) < 5:
                raise UniGrafixError(f"{source}: vertex needs a name and three coordinates")
            vertices[words[1]] = tuple(float(w) for w in words[2:5])
        elif kind == "c_rgb":
            if len(words) != 5:
                raise UniGrafixError(f"{source}: color needs a name and three components")
            colors[words[1]] = tuple(float(w) for w in words[2:5])
        elif kind == "f":
            if "(" not in words or ")" not in words:
                raise UniGrafixError(f"{source}: face {words[1]} has no vertex list")
            start = words.index("(")
            end = words.index(")")
            if "(" in words[end:]:
                raise UniGrafixError(f"{source}: face {words[1]} has more than one contour")
            names = words[start + 1 : end]
            trailing = words[end + 1 :]
            color = colors.get(trailing[0], DEFAULT_COLOR) if trailing else DEFAULT_COLOR
            try:
                points = [vertices[name] for name in names]
            except KeyError as missing:
                raise UniGrafixError(f"{source}: face {words[1]} uses unknown vertex {missing}")
            if len(points) >= 3:
                mesh.add_polygon(points, color)
        else:
            raise UniGrafixError(f"{source}: unsupported statement '{kind}'")
    return mesh


def main(argv):
    if len(argv) != 3:
        raise SystemExit("Usage: unigrafix.py INPUT.ug OUTPUT.off")
    mesh = flatten(Path(argv[1]).read_text(encoding="latin-1"), argv[1])
    mesh.write_off(argv[2], f"converted from {argv[1]}")
    print(f"{argv[2]}: {len(mesh.vertices)} vertices, {len(mesh.triangles)} triangles")


if __name__ == "__main__":
    main(sys.argv)
