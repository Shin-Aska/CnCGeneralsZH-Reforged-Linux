#!/usr/bin/env python3
"""Write the isolated cloud preview's flat fixture or water inspection yard.

Only Python's standard library is needed. The CkMp layout follows the engine's
RandomMapGenerator.cpp; the two invisible start waypoints let the ordinary map
cache recognize a two-player map. The default fixture has no buildings, units,
water, scenery, resource objects, or map scripts. The preview executable owns
its harmless props and walkers; --inspection-yard adds only a real water polygon
and a lowered pond bed, keeping the source positions and walking lanes flat.

Usage: generate_map.py /path/to/overlay/Maps/CloudPreview/CloudPreview.map --inspection-yard

Playable bounds are [0, 1280] on both axes, with centre (640, 640, 37.5).
The surrounding 20-cell border uses the same flat terrain.
"""

from contextlib import contextmanager
from pathlib import Path
import argparse
import math
import struct


PLAYABLE_CELLS = 128
BORDER_CELLS = 20
HEIGHT_BYTE = 60
WATER_LEVEL = 37  # PolygonTrigger stores integer world Z; dry ground is 37.5.


def yard_height(x, y):
    """Rounded pond bed; every sample outside its footprint stays exactly flat.

    The four walking lanes remain at y=592/624/656/688. The highest wet bank is
    y=565, leaving a dry margin below the closest lane. World height is byte*.625.
    """
    # Signed distance to a rounded rectangle centered on the lower half of the
    # inspection view. A broad bank exposes shallow water, deep water and shore.
    qx = abs(x - 640.0) - 285.0
    qy = abs(y - 472.5) - 37.5
    distance = math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - 55.0
    depth = max(0.0, min(1.0, -distance / 32.0))
    depth = depth * depth * (3.0 - 2.0 * depth)
    return HEIGHT_BYTE - round(44.0 * depth)


def write_water(writer):
    # PolygonTrigger::WritePolygonTriggersDataChunk v4: count, name, layer,
    # ID, water byte, river byte, river-start integer, count, XYZ integer points.
    # The polygon extends a little beyond the depression. Real terrain/depth
    # clipping defines the shoreline rather than exposing a straight water edge.
    points = ((335, 360), (945, 360), (1000, 415), (1000, 530),
              (945, 585), (335, 585), (280, 530), (280, 415))
    writer.integer(1)
    writer.ascii("CloudInspectionPond")
    writer.ascii("Cloud preview water")
    writer.integer(1)
    writer.body.extend(bytes((1, 0)))  # Real water, not a river or trigger zone.
    writer.integer(0)
    writer.integer(len(points))
    for x, y in points:
        for value in (x, y, WATER_LEVEL):
            writer.integer(value)


class MapWriter:
    def __init__(self):
        self.body = bytearray()
        self.names = {}

    def name_id(self, name):
        if name not in self.names:
            self.names[name] = len(self.names) + 1
        return self.names[name]

    def integer(self, value):
        self.body.extend(struct.pack("<i", value))

    def real(self, value):
        self.body.extend(struct.pack("<f", value))

    def ascii(self, value):
        encoded = value.encode("ascii")
        self.body.extend(struct.pack("<H", len(encoded)))
        self.body.extend(encoded)

    @contextmanager
    def chunk(self, name, version):
        self.body.extend(struct.pack("<IH", self.name_id(name), version))
        size_offset = len(self.body)
        self.integer(0)
        yield
        struct.pack_into("<i", self.body, size_offset, len(self.body) - size_offset - 4)

    def dictionary(self, entries):
        self.body.extend(struct.pack("<H", len(entries)))
        for key, value in entries.items():
            if isinstance(value, bool):
                value_type = 0
            elif isinstance(value, int):
                value_type = 1
            elif isinstance(value, str):
                value_type = 3
            else:
                raise TypeError("unsupported map dictionary value: %r" % value)
            self.integer((self.name_id(key) << 8) | value_type)
            if value_type == 0:
                self.body.append(int(value))
            elif value_type == 1:
                self.integer(value)
            else:
                self.ascii(value)

    def finish(self):
        output = bytearray(b"CkMp")
        output.extend(struct.pack("<i", len(self.names)))
        for name, name_id in self.names.items():
            encoded = name.encode("ascii")
            output.append(len(encoded))
            output.extend(encoded)
            output.extend(struct.pack("<I", name_id))
        output.extend(self.body)
        return output


def write_sides(writer):
    # Retain the ordinary skirmish side names, which GameLogic resolves when it
    # builds the preview's occupied, driverless slots.
    factions = (
        "Civilian", "America", "China", "GLA", "AmericaAirForceGeneral",
        "AmericaLaserGeneral", "AmericaSuperWeaponGeneral", "ChinaTankGeneral",
        "ChinaNukeGeneral", "ChinaInfantryGeneral", "GLADemolitionGeneral",
        "GLAToxinGeneral", "GLAStealthGeneral",
    )
    sides = [("", "", "Neutral")]
    for faction in factions:
        name = "PlyrCivilian" if faction == "Civilian" else "Skirmish" + faction
        sides.append((name, "Faction" + faction, name))

    with writer.chunk("SidesList", 3):
        writer.integer(len(sides))
        for name, faction, display_name in sides:
            writer.dictionary({
                "playerName": name,
                "playerIsHuman": False,
                "playerDisplayName": display_name,
                "playerFaction": faction,
                "playerAllies": "",
                "playerEnemies": "",
            })
            writer.integer(0)  # No build list.
        writer.integer(len(sides))
        for name, _, _ in sides:
            writer.dictionary({
                "teamName": "team" + name,
                "teamOwner": name,
                "teamIsSingleton": True,
            })
        with writer.chunk("PlayerScriptsList", 1):
            for _ in sides:
                with writer.chunk("ScriptList", 1):
                    pass


def write_waypoint(writer, number, x, y):
    name = "Player_%d_Start" % number
    with writer.chunk("Object", 3):
        for value in (x, y, 0.0, 0.0):
            writer.real(value)
        writer.integer(0)
        writer.ascii("*Waypoints/Waypoint")
        writer.dictionary({
            "objectInitialHealth": 100,
            "objectEnabled": True,
            "objectPowered": True,
            "objectRecruitableAI": True,
            "originalOwner": "team",
            "uniqueID": name,
            "objectLayer": "Start Points",
            "waypointID": number,
            "objectDestructible": True,
            "objectSellable": True,
            "objectRepairable": True,
            "waypointName": name,
        })


def build_map(inspection_yard=False):
    writer = MapWriter()
    width = PLAYABLE_CELLS + 2 * BORDER_CELLS
    area = width * width

    with writer.chunk("HeightMapData", 4):
        for value in (width, width, BORDER_CELLS, 1, PLAYABLE_CELLS, PLAYABLE_CELLS, area):
            writer.integer(value)
        if inspection_yard:
            writer.body.extend(yard_height((x - BORDER_CELLS) * 10, (y - BORDER_CELLS) * 10)
                               for y in range(width) for x in range(width))
        else:
            # Kept byte-for-byte compatible with flat runtime test fixtures.
            writer.body.extend(bytes([HEIGHT_BYTE]) * area)

    with writer.chunk("BlendTileData", 6):
        writer.integer(area)
        # Four source tiles, with four quadrants per tile, tile a 2x2 sheet.
        tiles = [(((x // 2) % 2 + 2 * ((y // 2) % 2)) << 2) + 2 * (y & 1) + (x & 1)
                 for y in range(width) for x in range(width)]
        writer.body.extend(struct.pack("<%dh" % area, *tiles))
        writer.body.extend(bytes(area * 2 * 3))  # No blend, extra blend, or cliff indices.
        for value in (4, 1, 1, 1, 0, 4, 2, 0):
            writer.integer(value)
        writer.ascii("DirtType1")
        writer.integer(0)  # No edge tiles.
        writer.integer(0)  # No edge texture classes.

    with writer.chunk("WorldInfo", 1):
        writer.dictionary({"weather": 0, "compression": 0})
    write_sides(writer)

    with writer.chunk("ObjectsList", 3):
        write_waypoint(writer, 1, 180.0, 180.0)
        write_waypoint(writer, 2, 1100.0, 1100.0)
    with writer.chunk("PolygonTriggers", 4):
        if inspection_yard:
            write_water(writer)
        else:
            writer.integer(0)

    with writer.chunk("GlobalLighting", 3):
        writer.integer(2)  # TIME_OF_DAY_AFTERNOON.
        for _ in range(4):
            # The same neutral daylight in every time slot, for consistent comparison.
            for ambient, diffuse in ((0.45, 0.55), (0.50, 0.50)):
                for value in (ambient, ambient, ambient, diffuse, diffuse, diffuse, -0.5, 0.5, -1.0):
                    writer.real(value)
            # Two inactive terrain lights, then two inactive object lights.
            for _ in range(4):
                for value in (0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0):
                    writer.real(value)

    with writer.chunk("WaypointsList", 1):
        writer.integer(0)
    return writer.finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Destination CloudPreview.map path")
    parser.add_argument("--inspection-yard", action="store_true",
                        help="Add the preview pond; leave default harness fixtures flat")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(build_map(args.inspection_yard))
    description = "pond inspection yard" if args.inspection_yard else "flat fixture"
    print("Wrote %s (%s; 1280 x 1280 world units; centre 640,640,37.5)"
          % (args.output, description))


if __name__ == "__main__":
    main()
