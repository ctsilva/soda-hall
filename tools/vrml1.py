#!/usr/bin/env python3
"""Flatten the Soda Hall VRML 1.0 files into triangle meshes with per-face colors.

This is a reader for the subset of VRML 1.0 that Kofler's ug2vrml exporter produced, not a
general VRML implementation. Supported nodes: Separator, Switch, Translation, Rotation, Scale,
MatrixTransform, Material, MaterialBinding, Coordinate3, IndexedFaceSet, and DEF/USE. Cameras
and lights are parsed and ignored. Every other node type is skipped with its children.

The result of `flatten` is a `Scene`: a shell mesh (geometry written directly into the file,
which for a room is its walls, floor, ceiling, doors, and windows) and a furniture mesh (every
face reached through a USE of a named object such as CHAIR1 or BOOKSHELF2), plus a count of
how many times each named object was placed. Polygons are triangulated by ear clipping so
concave room outlines survive; vertices are merged after transformation.

Conventions follow the VRML 1.0 specification: row vectors, transforms accumulate by
pre-multiplication within a Separator, and MatrixTransform stores its translation in the
fourth row.
"""

import gzip
import math
import re
from dataclasses import dataclass, field
from pathlib import Path

# Values per field for fields whose value is not bracketed and may be a bare identifier
# (TRUE, PER_FACE_INDEXED) rather than a number. Unlisted fields consume numeric tokens.
FIELD_ARITY = {
    "translation": 3,
    "scaleFactor": 3,
    "rotation": 4,
    "matrix": 16,
    "whichChild": 1,
    "value": 1,
    "on": 1,
    "intensity": 1,
    "color": 3,
    "direction": 3,
    "position": 3,
    "orientation": 4,
    "focalDistance": 1,
    "heightAngle": 1,
    "ambientColor": 3,
    "diffuseColor": 3,
    "specularColor": 3,
    "emissiveColor": 3,
    "shininess": 1,
    "transparency": 1,
}

DEFAULT_DIFFUSE = (0.8, 0.8, 0.8)
MERGE_DECIMALS = 3  # Vertices closer than a thousandth of an inch are the same vertex.

TOKEN_PATTERN = re.compile(r"[{}\[\]]|[^\s{}\[\],]+")
NUMBER_PATTERN = re.compile(r"^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$")


class VrmlError(Exception):
    pass


@dataclass
class Node:
    kind: str
    fields: dict = field(default_factory=dict)
    children: list = field(default_factory=list)
    name: str = None  # DEF name, when the node was defined with one.


def tokenize(text):
    tokens = []
    for line in text.splitlines():
        hash_index = line.find("#")
        if hash_index >= 0:
            line = line[:hash_index]
        tokens.extend(TOKEN_PATTERN.findall(line))
    return tokens


def is_number(token):
    return bool(NUMBER_PATTERN.match(token))


def parse(text):
    """Parses VRML text into (root nodes, DEF table)."""
    tokens = tokenize(text)
    defs = {}
    position = 0

    def parse_node():
        nonlocal position
        token = tokens[position]
        if token == "DEF":
            name = tokens[position + 1]
            position += 2
            node = parse_node()
            node.name = name
            defs[name] = node
            return node
        if token == "USE":
            name = tokens[position + 1]
            position += 2
            return Node("USE", {"name": [name]})
        kind = token
        position += 1
        if position >= len(tokens) or tokens[position] != "{":
            raise VrmlError(f"expected '{{' after node type {kind}")
        position += 1
        node = Node(kind)
        while tokens[position] != "}":
            token = tokens[position]
            is_child = token in ("DEF", "USE") or (
                position + 1 < len(tokens) and tokens[position + 1] == "{"
            )
            if is_child:
                node.children.append(parse_node())
                continue
            position += 1
            if tokens[position] == "[":
                position += 1
                start = position
                while tokens[position] != "]":
                    position += 1
                values = tokens[start:position]
                position += 1
            elif token in FIELD_ARITY:
                arity = FIELD_ARITY[token]
                values = tokens[position : position + arity]
                position += arity
            else:
                start = position
                while position < len(tokens) and is_number(tokens[position]):
                    position += 1
                values = tokens[start:position]
            node.fields[token] = values
        position += 1
        return node

    roots = []
    while position < len(tokens):
        roots.append(parse_node())
    return roots, defs


# Matrices are 4x4 nested lists in row-vector form: point' = point @ matrix.


def identity():
    return [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]


def multiply(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def translation_matrix(t):
    m = identity()
    m[3][0], m[3][1], m[3][2] = t
    return m


def scale_matrix(s):
    m = identity()
    m[0][0], m[1][1], m[2][2] = s
    return m


def rotation_matrix(axis, angle):
    x, y, z = axis
    length = math.sqrt(x * x + y * y + z * z)
    if length == 0:
        return identity()
    x, y, z = x / length, y / length, z / length
    c = math.cos(angle)
    s = math.sin(angle)
    t = 1 - c
    # Column-vector Rodrigues rotation, transposed for row vectors.
    column = [
        [t * x * x + c, t * x * y - s * z, t * x * z + s * y],
        [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
        [t * x * z - s * y, t * y * z + s * x, t * z * z + c],
    ]
    m = identity()
    for i in range(3):
        for j in range(3):
            m[i][j] = column[j][i]
    return m


def transform_point(p, m):
    x, y, z = p
    return (
        x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0],
        x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1],
        x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2],
    )


def floats(values):
    return [float(v) for v in values]


def triples(values):
    numbers = floats(values)
    return [tuple(numbers[i : i + 3]) for i in range(0, len(numbers) - 2, 3)]


class Mesh:
    """Triangles with one color each; vertices merged by rounded position."""

    def __init__(self):
        self.vertices = []
        self.triangles = []  # (a, b, c, (r, g, b))
        self._index = {}

    def vertex(self, p):
        key = tuple(round(c, MERGE_DECIMALS) for c in p)
        index = self._index.get(key)
        if index is None:
            index = len(self.vertices)
            self.vertices.append(key)
            self._index[key] = index
        return index

    def add_polygon(self, points, color):
        indices = [self.vertex(p) for p in points]
        for a, b, c in triangulate(points):
            if len({indices[a], indices[b], indices[c]}) == 3:
                self.triangles.append((indices[a], indices[b], indices[c], color))

    def extend(self, other):
        remap = [self.vertex(p) for p in other.vertices]
        for a, b, c, color in other.triangles:
            self.triangles.append((remap[a], remap[b], remap[c], color))

    def bounds(self):
        if not self.vertices:
            return None
        lo = [min(v[i] for v in self.vertices) for i in range(3)]
        hi = [max(v[i] for v in self.vertices) for i in range(3)]
        return lo, hi

    def write_off(self, path, comment=None):
        with open(path, "w", encoding="utf-8") as out:
            out.write("OFF\n")
            if comment:
                out.write(f"# {comment}\n")
            out.write(f"{len(self.vertices)} {len(self.triangles)} 0\n")
            for x, y, z in self.vertices:
                out.write(f"{x:g} {y:g} {z:g}\n")
            for a, b, c, (r, g, bl) in self.triangles:
                out.write(f"3 {a} {b} {c} {r:.3f} {g:.3f} {bl:.3f}\n")


def newell_normal(points):
    nx = ny = nz = 0.0
    for i, (x0, y0, z0) in enumerate(points):
        x1, y1, z1 = points[(i + 1) % len(points)]
        nx += (y0 - y1) * (z0 + z1)
        ny += (z0 - z1) * (x0 + x1)
        nz += (x0 - x1) * (y0 + y1)
    return nx, ny, nz


def triangulate(points):
    """Index triples covering the polygon; ear clipping for anything beyond a triangle."""
    n = len(points)
    if n < 3:
        return []
    if n == 3:
        return [(0, 1, 2)]
    normal = newell_normal(points)
    axis = max(range(3), key=lambda i: abs(normal[i]))
    u, v = [i for i in range(3) if i != axis]
    flat = [(p[u], p[v]) for p in points]
    if normal[axis] < 0:
        flat = [(-x, y) for x, y in flat]

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    def inside(p, a, b, c):
        return cross(a, b, p) >= 0 and cross(b, c, p) >= 0 and cross(c, a, p) >= 0

    remaining = list(range(n))
    result = []
    guard = 0
    while len(remaining) > 3 and guard < n * n:
        guard += 1
        clipped = False
        for i in range(len(remaining)):
            ia = remaining[i - 1]
            ib = remaining[i]
            ic = remaining[(i + 1) % len(remaining)]
            a, b, c = flat[ia], flat[ib], flat[ic]
            if cross(a, b, c) <= 0:
                continue
            if any(
                inside(flat[j], a, b, c) for j in remaining if j not in (ia, ib, ic)
            ):
                continue
            result.append((ia, ib, ic))
            del remaining[i]
            clipped = True
            break
        if not clipped:
            break
    if len(remaining) == 3:
        result.append(tuple(remaining))
    elif len(remaining) > 3:
        # Degenerate (collinear or self-touching) outline: fall back to a fan.
        result.extend(
            (remaining[0], remaining[i], remaining[i + 1]) for i in range(1, len(remaining) - 1)
        )
    return result


@dataclass
class State:
    matrix: list
    coordinates: list = None
    diffuse: list = None
    binding: str = "OVERALL"
    _cache: tuple = None  # (id(coordinate node), matrix key) -> transformed points

    def copy(self):
        return State(self.matrix, self.coordinates, self.diffuse, self.binding, self._cache)


@dataclass
class Scene:
    shell: Mesh
    furniture: Mesh
    objects: dict  # DEF name -> number of placements at the top level of the file
    source: str


def flatten(text, source=""):
    roots, defs = parse(text)
    scene = Scene(Mesh(), Mesh(), {}, source)
    transformed = {}

    def faces(node, state, mesh):
        if not state.coordinates:
            return
        key = (id(state.coordinates), tuple(map(tuple, state.matrix)))
        points = transformed.get(key)
        if points is None:
            points = [transform_point(p, state.matrix) for p in state.coordinates]
            transformed[key] = points
        diffuse = state.diffuse or [DEFAULT_DIFFUSE]
        material_index = [int(v) for v in node.fields.get("materialIndex", [])]
        polygon = []
        face_number = 0
        for token in node.fields.get("coordIndex", []) + ["-1"]:
            index = int(token)
            if index >= 0:
                polygon.append(points[index])
                continue
            if len(polygon) >= 3:
                if state.binding == "PER_FACE_INDEXED" and material_index:
                    color = diffuse[material_index[face_number] % len(diffuse)]
                elif state.binding in ("PER_FACE", "PER_FACE_INDEXED"):
                    color = diffuse[face_number % len(diffuse)]
                else:
                    color = diffuse[0]
                mesh.add_polygon(polygon, color)
            polygon = []
            face_number += 1

    def visit(node, state, placed):
        kind = node.kind
        if kind == "Separator":
            inner = state.copy()
            for child in node.children:
                visit(child, inner, placed)
        elif kind == "Switch":
            which = int(node.fields.get("whichChild", ["-1"])[0])
            if 0 <= which < len(node.children):
                visit(node.children[which], state, placed)
        elif kind == "USE":
            name = node.fields["name"][0]
            target = defs.get(name)
            if target is None:
                raise VrmlError(f"USE of undefined name {name}")
            if placed is None:
                scene.objects[name] = scene.objects.get(name, 0) + 1
            visit(target, state, placed or name)
        elif kind == "Translation":
            translation = translation_matrix(floats(node.fields["translation"]))
            state.matrix = multiply(translation, state.matrix)
        elif kind == "Scale":
            state.matrix = multiply(scale_matrix(floats(node.fields["scaleFactor"])), state.matrix)
        elif kind == "Rotation":
            values = floats(node.fields["rotation"])
            state.matrix = multiply(rotation_matrix(values[:3], values[3]), state.matrix)
        elif kind == "MatrixTransform":
            values = floats(node.fields["matrix"])
            matrix = [values[i : i + 4] for i in range(0, 16, 4)]
            state.matrix = multiply(matrix, state.matrix)
        elif kind == "Material":
            if "diffuseColor" in node.fields:
                state.diffuse = triples(node.fields["diffuseColor"]) or [DEFAULT_DIFFUSE]
        elif kind == "MaterialBinding":
            state.binding = node.fields.get("value", ["OVERALL"])[0]
        elif kind == "Coordinate3":
            state.coordinates = triples(node.fields.get("point", []))
        elif kind == "IndexedFaceSet":
            faces(node, state, scene.furniture if placed else scene.shell)

    for root in roots:
        visit(root, State(identity()), None)
    return scene


def read_text(path):
    path = Path(path)
    if path.suffix == ".gz":
        with gzip.open(path, "rt", encoding="latin-1") as handle:
            return handle.read()
    return path.read_text(encoding="latin-1")


def main(argv):
    if len(argv) != 3:
        raise SystemExit("Usage: vrml1.py INPUT.wrl[.gz] OUTPUT_PREFIX\n"
                         "Writes OUTPUT_PREFIX.shell.off and OUTPUT_PREFIX.furniture.off")
    scene = flatten(read_text(argv[1]), argv[1])
    scene.shell.write_off(argv[2] + ".shell.off", f"shell of {argv[1]}")
    scene.furniture.write_off(argv[2] + ".furniture.off", f"furniture of {argv[1]}")
    print(f"shell: {len(scene.shell.vertices)} vertices, {len(scene.shell.triangles)} triangles")
    print(f"furniture: {len(scene.furniture.vertices)} vertices, "
          f"{len(scene.furniture.triangles)} triangles")
    for name, count in sorted(scene.objects.items()):
        print(f"  {name} x{count}")


if __name__ == "__main__":
    import sys

    main(sys.argv)
