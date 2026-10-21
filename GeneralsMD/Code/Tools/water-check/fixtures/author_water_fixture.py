#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = []
# ///
from __future__ import annotations

import hashlib
import struct
import sys
import tempfile
from pathlib import Path


class FixtureError(Exception):
    pass


class ChunkWriter:
    def __init__(self) -> None:
        self.body: bytearray = bytearray()
        self.names: list[str] = []
        self.open_chunks: list[int] = []

    def _id(self, name: str) -> int:
        if name not in self.names:
            self.names.append(name)
        return self.names.index(name) + 1

    def raw(self, value: bytes) -> None:
        self.body.extend(value)

    def integer(self, value: int) -> None:
        self.raw(struct.pack("<i", value))

    def real(self, value: float) -> None:
        self.raw(struct.pack("<f", value))

    def byte(self, value: int) -> None:
        self.raw(bytes((value,)))

    def text(self, value: str) -> None:
        encoded = value.encode("ascii")
        self.raw(struct.pack("<H", len(encoded)) + encoded)

    def open(self, name: str, version: int) -> None:
        self.raw(struct.pack("<IH", self._id(name), version))
        self.open_chunks.append(len(self.body))
        self.integer(0)

    def close(self) -> None:
        if not self.open_chunks:
            raise FixtureError("close without an open chunk")
        size_at = self.open_chunks.pop()
        struct.pack_into("<i", self.body, size_at, len(self.body) - size_at - 4)

    def begin_dict(self, pairs: int) -> None:
        self.raw(struct.pack("<H", pairs))

    def _dict_key(self, key: str, kind: int) -> None:
        self.integer((self._id(key) << 8) | kind)

    def dict_bool(self, key: str, value: bool) -> None:
        self._dict_key(key, 0)
        self.byte(int(value))

    def dict_int(self, key: str, value: int) -> None:
        self._dict_key(key, 1)
        self.integer(value)

    def dict_text(self, key: str, value: str) -> None:
        self._dict_key(key, 3)
        self.text(value)

    def finish(self) -> bytes:
        if self.open_chunks:
            raise FixtureError("unclosed chunk")
        toc = bytearray(b"CkMp" + struct.pack("<i", len(self.names)))
        for identifier, name in enumerate(self.names, 1):
            encoded = name.encode("ascii")
            toc.extend(bytes((len(encoded),)) + encoded + struct.pack("<I", identifier))
        return bytes(toc + self.body)


def waypoint(writer: ChunkWriter, name: str, identifier: int, x: float, y: float) -> None:
    writer.open("Object", 3)
    for value in (x, y, 0.0, 0.0):
        writer.real(value)
    writer.integer(0)
    writer.text("*Waypoints/Waypoint")
    writer.begin_dict(12)
    writer.dict_int("objectInitialHealth", 100)
    writer.dict_bool("objectEnabled", True)
    writer.dict_bool("objectPowered", True)
    writer.dict_bool("objectRecruitableAI", True)
    writer.dict_text("originalOwner", "team")
    writer.dict_text("uniqueID", name)
    writer.dict_text("objectLayer", "Start Points")
    writer.dict_int("waypointID", identifier)
    writer.dict_bool("objectDestructible", True)
    writer.dict_bool("objectSellable", True)
    writer.dict_bool("objectRepairable", True)
    writer.dict_text("waypointName", name)
    writer.close()


def map_object(writer: ChunkWriter, template: str, owner: str, identifier: str, x: float, y: float) -> None:
    writer.open("Object", 3)
    for value in (x, y, 0.0, 0.0):
        writer.real(value)
    writer.integer(0)
    writer.text(template)
    writer.begin_dict(11)
    writer.dict_int("objectInitialHealth", 100)
    writer.dict_bool("objectEnabled", True)
    writer.dict_bool("objectIndestructible", False)
    writer.dict_bool("objectUnsellable", False)
    writer.dict_bool("objectPowered", True)
    writer.dict_bool("objectRecruitableAI", True)
    writer.dict_text("objectName", "")
    writer.dict_text("originalOwner", owner)
    writer.dict_text("uniqueID", identifier)
    writer.dict_text("objectLayer", "")
    writer.dict_bool("objectSelectable", True)
    writer.close()


def sides(writer: ChunkWriter) -> None:
    factions = (
        ("PlyrCivilian", "FactionCivilian"), ("SkirmishAmerica", "FactionAmerica"),
        ("SkirmishChina", "FactionChina"), ("SkirmishGLA", "FactionGLA"),
        ("SkirmishAmericaAirForceGeneral", "FactionAmericaAirForceGeneral"),
        ("SkirmishAmericaLaserGeneral", "FactionAmericaLaserGeneral"),
        ("SkirmishAmericaSuperWeaponGeneral", "FactionAmericaSuperWeaponGeneral"),
        ("SkirmishChinaTankGeneral", "FactionChinaTankGeneral"),
        ("SkirmishChinaNukeGeneral", "FactionChinaNukeGeneral"),
        ("SkirmishChinaInfantryGeneral", "FactionChinaInfantryGeneral"),
        ("SkirmishGLADemolitionGeneral", "FactionGLADemolitionGeneral"),
        ("SkirmishGLAToxinGeneral", "FactionGLAToxinGeneral"),
        ("SkirmishGLAStealthGeneral", "FactionGLAStealthGeneral"),
    )
    writer.open("SidesList", 3)
    writer.integer(len(factions) + 1)
    for name, faction in (("", ""), *factions):
        writer.begin_dict(6)
        writer.dict_text("playerName", name)
        writer.dict_bool("playerIsHuman", False)
        writer.dict_text("playerDisplayName", "Neutral" if not name else name)
        writer.dict_text("playerFaction", faction)
        writer.dict_text("playerAllies", "")
        writer.dict_text("playerEnemies", "")
        writer.integer(0)
    writer.integer(len(factions) + 1)
    for name, _faction in (("", ""), *factions):
        writer.begin_dict(3)
        writer.dict_text("teamName", "team" if not name else f"team{name}")
        writer.dict_text("teamOwner", name)
        writer.dict_bool("teamIsSingleton", True)
    writer.open("PlayerScriptsList", 1)
    for _ in range(len(factions) + 1):
        writer.open("ScriptList", 1)
        writer.close()
    writer.close()
    writer.close()


def water(writer: ChunkWriter) -> None:
    polygons = (
        ("ConcaveShore", 25, ((160, 160), (920, 160), (920, 410), (650, 410),
            (650, 690), (920, 690), (920, 940), (160, 940), (160, 690),
            (430, 690), (430, 410), (160, 410))),
        ("DisconnectedPool", 25, ((990, 730), (1140, 730), (1140, 930), (990, 930))),
        ("RaisedPool", 52, ((960, 190), (1140, 190), (1140, 420), (960, 420))),
    )
    writer.open("PolygonTriggers", 4)
    writer.integer(len(polygons))
    for identifier, (name, level, points) in enumerate(polygons, 1):
        writer.text(name)
        writer.text("")
        writer.integer(identifier)
        writer.byte(1)
        writer.byte(0)
        writer.integer(0)
        writer.integer(len(points))
        for x, y in points:
            for value in (x, y, level):
                writer.integer(value)
    writer.close()


def author() -> bytes:
    writer = ChunkWriter()
    width = height = 161
    count = width * height
    writer.open("HeightMapData", 4)
    for value in (width, height, 20, 1, 120, 120, count):
        writer.integer(value)
    writer.raw(bytes((10,)) * count)
    writer.close()
    writer.open("BlendTileData", 6)
    writer.integer(count)
    writer.raw(bytes(count * 8))
    for value in (16, 1, 1, 4):
        writer.integer(value)
    for first, texture in enumerate(("SandLargeType2", "SandType3wet", "DirtMediumType3", "RocksType3")):
        for value in (first * 4, 4, 2, 0):
            writer.integer(value)
        writer.text(texture)
    writer.integer(0)
    writer.integer(0)
    writer.close()
    writer.open("WorldInfo", 1)
    writer.begin_dict(2)
    writer.dict_int("weather", 0)
    writer.dict_int("compression", 0)
    writer.close()
    sides(writer)
    writer.open("ObjectsList", 3)
    waypoint(writer, "Player_1_Start", 1, 1080.0, 1080.0)
    waypoint(writer, "Player_2_Start", 2, 1080.0, 560.0)
    waypoint(writer, "InitialCameraPosition", 3, 560.0, 300.0)
    waypoint(writer, "WaveGuide1", 4, 200.0, 200.0)
    map_object(writer, "AmericaCommandCenter", "teamSkirmishAmerica", "Fixture USA base", 1080.0, 1080.0)
    map_object(writer, "ChinaCommandCenter", "teamSkirmishChina", "Fixture China base", 1080.0, 560.0)
    writer.close()
    water(writer)
    writer.open("GlobalLighting", 3)
    writer.integer(1)
    light = (0.40, 0.37, 0.32, 0.92, 0.84, 0.66, -0.45, 0.30, -0.84)
    dark = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0)
    for _hour in range(4):
        for _subject in range(2):
            for values in (light, dark, dark):
                for value in values:
                    writer.real(value)
    writer.close()
    writer.open("WaypointsList", 1)
    writer.integer(0)
    writer.close()
    return writer.finish()


def validate_objects(data: bytes, names: dict[int, str], at: int, size: int) -> None:
    end = at + size
    records: dict[str, tuple[str, int | str | bool]] = {}
    count = 0
    while at < end:
        if at + 10 > end:
            raise FixtureError("truncated object chunk header")
        identifier, version, object_size = struct.unpack_from("<IHi", data, at)
        at += 10
        object_end = at + object_size
        if names.get(identifier) != "Object" or version != 3 or object_size < 22 or object_end > end:
            raise FixtureError("invalid Object v3 boundary")
        at += 20
        text_length = struct.unpack_from("<H", data, at)[0]
        at += 2
        if at + text_length + 2 > object_end:
            raise FixtureError("truncated object template")
        template = data[at:at + text_length].decode("ascii")
        at += text_length
        pair_count = struct.unpack_from("<H", data, at)[0]
        at += 2
        properties: dict[str, int | str | bool] = {}
        for _ in range(pair_count):
            if at + 4 > object_end:
                raise FixtureError("truncated object dictionary key")
            key_and_type = struct.unpack_from("<i", data, at)[0]
            at += 4
            key = names.get(key_and_type >> 8)
            kind = key_and_type & 0xff
            if key is None or kind not in (0, 1, 3):
                raise FixtureError("invalid object dictionary entry")
            if kind == 0:
                if at >= object_end:
                    raise FixtureError("truncated object boolean")
                value: int | str | bool = bool(data[at])
                at += 1
            elif kind == 1:
                if at + 4 > object_end:
                    raise FixtureError("truncated object integer")
                value = struct.unpack_from("<i", data, at)[0]
                at += 4
            else:
                if at + 2 > object_end:
                    raise FixtureError("truncated object string length")
                length = struct.unpack_from("<H", data, at)[0]
                at += 2
                if at + length > object_end:
                    raise FixtureError("truncated object string")
                value = data[at:at + length].decode("ascii")
                at += length
            properties[key] = value
        if at != object_end:
            raise FixtureError("unexpected Object v3 payload")
        unique = properties.get("uniqueID")
        if not isinstance(unique, str) or unique in records:
            raise FixtureError("missing or duplicate object uniqueID")
        records[unique] = (template, properties.get("waypointID", properties.get("originalOwner", "")))
        count += 1
    expected = {
        "Player_1_Start": ("*Waypoints/Waypoint", 1),
        "Player_2_Start": ("*Waypoints/Waypoint", 2),
        "InitialCameraPosition": ("*Waypoints/Waypoint", 3),
        "WaveGuide1": ("*Waypoints/Waypoint", 4),
        "Fixture USA base": ("AmericaCommandCenter", "teamSkirmishAmerica"),
        "Fixture China base": ("ChinaCommandCenter", "teamSkirmishChina"),
    }
    if count != 6 or records != expected:
        raise FixtureError("fixture ObjectsList identity or ownership mismatch")


def validate(data: bytes) -> str:
    if data[:4] != b"CkMp" or len(data) < 12:
        raise FixtureError("expected an uncompressed CkMp stream")
    names: dict[int, str] = {}
    at = 8
    name_count = struct.unpack_from("<i", data, 4)[0]
    if name_count < 0:
        raise FixtureError("negative table-of-contents count")
    for _ in range(name_count):
        if at >= len(data):
            raise FixtureError("truncated table of contents")
        length = data[at]
        at += 1
        if at + length + 4 > len(data):
            raise FixtureError("truncated table-of-contents entry")
        name = data[at:at + length].decode("ascii")
        at += length
        identifier = struct.unpack_from("<I", data, at)[0]
        at += 4
        names[identifier] = name
    chunks: list[str] = []
    object_lists = 0
    while at < len(data):
        if at + 10 > len(data):
            raise FixtureError("truncated top-level chunk header")
        identifier, _version, size = struct.unpack_from("<IHi", data, at)
        at += 10
        if size < 0 or at + size > len(data) or identifier not in names:
            raise FixtureError("invalid top-level chunk boundary or name")
        chunk_name = names[identifier]
        chunks.append(chunk_name)
        if chunk_name == "ObjectsList":
            validate_objects(data, names, at, size)
            object_lists += 1
        at += size
    required = {"HeightMapData", "BlendTileData", "WorldInfo", "SidesList", "ObjectsList",
                "PolygonTriggers", "GlobalLighting", "WaypointsList"}
    if at != len(data) or not required.issubset(chunks) or object_lists != 1:
        raise FixtureError("missing required map chunk")
    if any(token not in data for token in (b"ConcaveShore", b"DisconnectedPool", b"RaisedPool")):
        raise FixtureError("fixture identity is missing")
    return hashlib.sha256(data).hexdigest()


def main(arguments: list[str]) -> int:
    if arguments == ["--selfcheck"]:
        first = author()
        second = author()
        if first != second:
            raise FixtureError("authoring is not deterministic")
        _ = validate(first)
        try:
            _ = validate(first[:-1])
        except FixtureError:
            template = b"*Waypoints/Waypoint"
            object_at = first.index(struct.pack("<H", len(template)) + template) - 30
            nested = bytearray(first)
            object_size = struct.unpack_from("<i", nested, object_at + 6)[0]
            struct.pack_into("<i", nested, object_at + 6, object_size - 1)
            try:
                _ = validate(bytes(nested))
            except FixtureError:
                return 0
            raise FixtureError("truncated nested Object v3 was accepted")
        raise FixtureError("truncated top-level map was accepted")
    if len(arguments) == 2 and arguments[0] == "--check":
        print(validate(Path(arguments[1]).read_bytes()))
        return 0
    if len(arguments) != 1:
        raise FixtureError("expected OUTPUT.map, --check OUTPUT.map, or --selfcheck")
    output = Path(arguments[0])
    data = author()
    _ = validate(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as pending:
        _ = pending.write(data)
        temporary = Path(pending.name)
    _ = temporary.replace(output)
    print(validate(output.read_bytes()))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (FixtureError, OSError, UnicodeError, struct.error) as error:
        print(f"water fixture: {error}", file=sys.stderr)
        raise SystemExit(2) from error
