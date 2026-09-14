#!/usr/bin/env python3
"""Build the cleaned Soda Hall dataset from Kofler's VRML JumpThru mirror.

Reads the mirrored site (the directory that contains `floors-standard/` and `rooms-standard/`)
and writes a self-describing dataset:

    data/
      manifest.json                 every floor and room with paths, bounds, and counts
      floors/floor-3.shell.off      whole-floor walls, from Kofler's per-floor files
      floors/floor-3.furniture.off  every room's furniture on that floor, merged
      rooms/floor-3/room-319.shell.off
      rooms/floor-3/room-319.furniture.off
      floorplans/floor-3.gif        the site's floor plan bitmaps
      walkthru/building.shell.off   the 1994 WALKTHRU UniGrafix models, if source/walkthru
      walkthru/floor-5.shell.off    holds them (they are not part of Kofler's site)
      source/                       the original .wrl.gz files, untouched

Meshes are triangulated OFF files with an RGB color after each face, in the building's
own coordinate frame (inches, Z up), so any floor or room can be loaded together without
further transformation. Room names follow the building's numbering: `room-319` is room 319,
`room-283a` a lettered sub-room, and `stairs-3-1` the first stairwell drawn on floor 3.

Usage: build_dataset.py MIRROR_DIR OUTPUT_DIR [--jobs N]
"""

import json
import re
import shutil
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import unigrafix  # noqa: E402
import vrml1  # noqa: E402

FLOOR_NUMBERS = range(2, 8)
# The WALKTHRU files that may sit in source/walkthru, with their dataset ids and names.
WALKTHRU_MODELS = [
    ("csb3r.macro.ug", "walkthru-building", "WALKTHRU model, whole building (1994)"),
    ("csb5.macro.ug", "walkthru-floor-5", "WALKTHRU model, floor 5 (1994)"),
]
ROOM_FILE = re.compile(r"^room(\d)(stair(\d)|[0-9a-z]+)\.wrl\.gz$")
FLOOR_PLAN_NAMES = {2: "2ndfloor", 3: "3rdfloor", 4: "4thfloor", 5: "5thfloor", 6: "6thfloor",
                    7: "7thfloor"}


def room_identity(filename):
    """Maps a source file name to (floor number, dataset id, display name)."""
    match = ROOM_FILE.match(filename)
    if not match:
        raise SystemExit(f"unexpected room file name {filename}")
    floor = int(match.group(1))
    if match.group(3):
        stair = int(match.group(3))
        return floor, f"stairs-{floor}-{stair}", f"Stairwell {stair}"
    number = match.group(1) + match.group(2)
    return floor, f"room-{number}", f"Room {number}"


def bounds_json(mesh):
    box = mesh.bounds()
    return None if box is None else {"min": box[0], "max": box[1]}


def merged_bounds(boxes):
    boxes = [b for b in boxes if b]
    if not boxes:
        return None
    return {
        "min": [min(b["min"][i] for b in boxes) for i in range(3)],
        "max": [max(b["max"][i] for b in boxes) for i in range(3)],
    }


def convert_room(arguments):
    """Worker: converts one room file and returns its manifest entry plus furniture mesh."""
    source, output_dir, floor, room_id, name = arguments
    scene = vrml1.flatten(vrml1.read_text(source), source.name)
    room_dir = output_dir / "rooms" / f"floor-{floor}"
    room_dir.mkdir(parents=True, exist_ok=True)
    shell_path = room_dir / f"{room_id}.shell.off"
    furniture_path = room_dir / f"{room_id}.furniture.off"
    scene.shell.write_off(shell_path, f"Soda Hall {name}, floor {floor}: shell, from {source.name}")
    scene.furniture.write_off(
        furniture_path, f"Soda Hall {name}, floor {floor}: furniture, from {source.name}"
    )
    entry = {
        "id": room_id,
        "name": name,
        "floor": floor,
        "source": f"source/rooms-standard/floor{floor}/{source.name}",
        "shell": {
            "path": str(shell_path.relative_to(output_dir)),
            "vertices": len(scene.shell.vertices),
            "triangles": len(scene.shell.triangles),
            "bounds": bounds_json(scene.shell),
        },
        "furniture": {
            "path": str(furniture_path.relative_to(output_dir)),
            "vertices": len(scene.furniture.vertices),
            "triangles": len(scene.furniture.triangles),
            "bounds": bounds_json(scene.furniture),
        },
        "objects": dict(sorted(scene.objects.items())),
    }
    return entry, scene.furniture


def convert_floor_shell(source, output_dir, floor):
    scene = vrml1.flatten(vrml1.read_text(source), source.name)
    path = output_dir / "floors" / f"floor-{floor}.shell.off"
    scene.shell.write_off(
        path, f"Soda Hall floor {floor}: shell without furniture, from {source.name}"
    )
    return {
        "path": str(path.relative_to(output_dir)),
        "vertices": len(scene.shell.vertices),
        "triangles": len(scene.shell.triangles),
        "bounds": bounds_json(scene.shell),
    }


def convert_walkthru(output_dir):
    """Converts whichever WALKTHRU files are present under source/walkthru."""
    source_dir = output_dir / "source" / "walkthru"
    models = []
    for filename, model_id, name in WALKTHRU_MODELS:
        source = source_dir / filename
        if not source.exists():
            continue
        mesh = unigrafix.flatten(source.read_text(encoding="latin-1"), source.name)
        path = output_dir / "walkthru" / f"{model_id.removeprefix('walkthru-')}.shell.off"
        path.parent.mkdir(parents=True, exist_ok=True)
        mesh.write_off(path, f"Soda Hall {name}, from {source.name}")
        models.append({
            "id": model_id,
            "name": name,
            "source": f"source/walkthru/{filename}",
            "shell": {
                "path": str(path.relative_to(output_dir)),
                "vertices": len(mesh.vertices),
                "triangles": len(mesh.triangles),
                "bounds": bounds_json(mesh),
            },
        })
        print(f"{model_id}: {len(mesh.triangles)} triangles from {filename}")
    return models


def copy_sources(mirror, output_dir):
    """Copies the original VRML files and floor plans, dropping the Apache index pages."""
    source_dir = output_dir / "source"
    for relative in ("floors-standard", "rooms-standard"):
        for path in (mirror / relative).rglob("*.wrl.gz"):
            target = source_dir / path.relative_to(mirror)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    for html in ("index.html", "floors-standard/floors.html", "rooms-standard/standard.html"):
        if (mirror / html).exists():
            target = source_dir / html
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(mirror / html, target)
    plans = output_dir / "floorplans"
    plans.mkdir(exist_ok=True)
    for floor, stem in FLOOR_PLAN_NAMES.items():
        gif = mirror / "floors-standard" / f"{stem}.gif"
        if gif.exists():
            shutil.copy2(gif, plans / f"floor-{floor}.gif")


def main(argv):
    arguments = [a for a in argv[1:] if not a.startswith("--")]
    jobs = None
    for flag in (a for a in argv[1:] if a.startswith("--")):
        if flag.startswith("--jobs="):
            jobs = int(flag.split("=", 1)[1])
        else:
            raise SystemExit(__doc__)
    if len(arguments) != 2:
        raise SystemExit(__doc__)
    mirror = Path(arguments[0])
    output_dir = Path(arguments[1])
    if not (mirror / "rooms-standard").is_dir():
        raise SystemExit(f"{mirror} does not contain rooms-standard/")
    (output_dir / "floors").mkdir(parents=True, exist_ok=True)

    copy_sources(mirror, output_dir)

    tasks = []
    for floor in FLOOR_NUMBERS:
        for source in sorted((mirror / "rooms-standard" / f"floor{floor}").glob("*.wrl.gz")):
            file_floor, room_id, name = room_identity(source.name)
            if file_floor != floor:
                raise SystemExit(f"{source} is filed under floor {floor}")
            tasks.append((source, output_dir, floor, room_id, name))

    with ProcessPoolExecutor(max_workers=jobs) as pool:
        results = list(pool.map(convert_room, tasks))

    floors = []
    for floor in FLOOR_NUMBERS:
        shell_source = mirror / "floors-standard" / f"room{floor}.wrl.gz"
        shell = convert_floor_shell(shell_source, output_dir, floor)
        rooms = []
        furniture = vrml1.Mesh()
        for entry, mesh in results:
            if entry["floor"] == floor:
                rooms.append(entry)
                furniture.extend(mesh)
        furniture_path = output_dir / "floors" / f"floor-{floor}.furniture.off"
        furniture.write_off(furniture_path, f"Soda Hall floor {floor}: furniture of every room")
        plan = output_dir / "floorplans" / f"floor-{floor}.gif"
        floors.append({
            "number": floor,
            "name": f"Floor {floor}",
            "source": f"source/floors-standard/room{floor}.wrl.gz",
            "floorplan": str(plan.relative_to(output_dir)) if plan.exists() else None,
            "shell": shell,
            "furniture": {
                "path": str(furniture_path.relative_to(output_dir)),
                "vertices": len(furniture.vertices),
                "triangles": len(furniture.triangles),
                "bounds": bounds_json(furniture),
            },
            "bounds": merged_bounds([shell["bounds"], bounds_json(furniture)]),
            "rooms": rooms,
        })
        print(f"floor {floor}: {len(rooms)} rooms, shell {shell['triangles']} triangles, "
              f"furniture {len(furniture.triangles)} triangles")

    manifest = {
        "name": "Soda Hall",
        "description": "UC Berkeley's computer science building, from Michael Kofler's "
                       "VRML JumpThru model (1996-1998), flattened to triangle meshes.",
        "units": "inches",
        "up": "z",
        "bounds": merged_bounds([f["bounds"] for f in floors]),
        "floors": floors,
        "walkthru": convert_walkthru(output_dir),
    }
    with open(output_dir / "manifest.json", "w", encoding="utf-8") as out:
        json.dump(manifest, out, indent=1)
    total = sum(r["shell"]["triangles"] + r["furniture"]["triangles"]
                for f in floors for r in f["rooms"])
    print(f"wrote {output_dir / 'manifest.json'}: {sum(len(f['rooms']) for f in floors)} rooms, "
          f"{total} room triangles")


if __name__ == "__main__":
    main(sys.argv)
