#!/usr/bin/env python3
"""Write the EMP demo map: a flat yard where EMPs go off on their own, over parked targets.

Only Python's standard library is needed.  The terrain and sides are the cloud preview's flat
fixture (GeneralsMD/Code/Tools/cloud_preview/generate_map.py); this adds the objects and puts the
lighting of a retail map in (GC_SuperWeaponsGeneral's, morning), because the disabled tint is a
fixed -0.5 on every light and on the flat fixture's neutral 0.5 lights it went to plain black.  The
map.ini next to the map (written here too) defines the two beacons:

  EMPDemoPulseBeacon    (640, 640)  every 10 s calls the China EMP Pulse on itself: the power's
                                    own SUPERWEAPON_EMPPulse, a cargo plane from the nearest map
                                    edge dropping EMPPulseBomb, whose death runs
                                    WeaponFX_EMPPulseImpact and OCL_EMPPulseEffectSpheroids.
  EMPDemoPatriotBeacon  (850, 640)  every 4 s the Superweapon General's EMP Patriot hit, 8 ft up:
                                    WeaponFX_EMPRocketDetonation and
                                    SupW_OCL_PatriotEMPEffectSpheroid.

Parked civilian-owned tanks, a Humvee and a power plant ring each beacon, so the disabled look
(the tint and the EMPSparks systems) shows too.  Nothing fires a weapon and nothing is scripted.

Usage: make_emp_demo.py <folder>      writes <folder>/Maps/EMPDemo/EMPDemo.map and map.ini

Run it with:  -overlay <folder> -map "Maps\\EMPDemo\\EMPDemo.map"
"""

from pathlib import Path
import importlib.util
import struct
import sys

REPO = Path(__file__).resolve().parents[4]
GENERATOR = REPO / "GeneralsMD/Code/Tools/cloud_preview/generate_map.py"

spec = importlib.util.spec_from_file_location("generate_map", GENERATOR)
generate_map = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generate_map)

PULSE = (640.0, 640.0)
PATRIOT = (850.0, 640.0)

# (template, x, y, facing in radians); all civilian, parked
TARGETS = [
    ("AmericaTankCrusader",   PULSE[0] - 70, PULSE[1] - 20, 0.6),
    ("ChinaTankOverlord",     PULSE[0] + 40, PULSE[1] - 80, 2.0),
    ("GLATankScorpion",       PULSE[0] + 60, PULSE[1] + 50, -2.4),
    ("AmericaVehicleHumvee",  PULSE[0] - 30, PULSE[1] + 75, -1.0),
    ("ChinaTankBattleMaster", PULSE[0] - 110, PULSE[1] + 40, 0.0),
    ("AmericaPowerPlant",     PULSE[0] - 20, PULSE[1] - 150, 1.57),
    ("AmericaTankCrusader",   PATRIOT[0] + 12, PATRIOT[1] - 6, 3.0),
    ("ChinaTankBattleMaster", PATRIOT[0] - 10, PATRIOT[1] + 14, 1.2),
]

MAP_INI = """\
; EMP demo (docs/reference/emp-effect-2026-11-11).  Two invisible beacons set the game's own EMP
; effects off on a timer; everything they make is EA's (Object/System.ini, WeaponObjects.ini,
; FXList.ini, ObjectCreationList.ini).  Nothing else of the game is changed.

ObjectCreationList OCL_EMPDemoPatriotHit
  CreateObject
    ObjectNames = EMPDemoPatriotBurst
    Count = 1
    Disposition = LIKE_EXISTING
    Offset = X:0 Y:0 Z:8
  End
End

; The Patriot missile's death without the missile: WeaponFX_EMPRocketDetonation is the weapon's
; ProjectileDetonationFX in WeaponReforged.ini/Weapon.ini, the OCL its ProjectileDetonationOCL.
Object EMPDemoPatriotBurst
  EditorSorting = SYSTEM
  KindOf = PROJECTILE NO_COLLIDE
  Geometry = SPHERE
  GeometryMajorRadius = 1.0
  GeometryIsSmall = Yes
  ; a map.ini may only add modules inside AddModule (ThingTemplate::parseModuleName)
  AddModule ModuleTag_EMPDemo
    Draw = W3DModelDraw ModuleTag_01
      DefaultConditionState
        Model = None
      End
    End
    Body = ActiveBody ModuleTag_02
      MaxHealth = 1.0
      InitialHealth = 1.0
    End
    Behavior = LifetimeUpdate ModuleTag_03
      MinLifetime = 1
      MaxLifetime = 1
    End
    Behavior = CreateObjectDie ModuleTag_04
      CreationList = SupW_OCL_PatriotEMPEffectSpheroid
    End
    Behavior = FXListDie ModuleTag_05
      DeathFX = WeaponFX_EMPRocketDetonation
      OrientToObject = No
    End
    Behavior = DestroyDie ModuleTag_06
    End
  End
End

Object EMPDemoPulseBeacon
  EditorSorting = SYSTEM
  KindOf = NO_COLLIDE IMMOBILE UNATTACKABLE
  Geometry = SPHERE
  GeometryMajorRadius = 1.0
  GeometryIsSmall = Yes
  ; a map.ini may only add modules inside AddModule (ThingTemplate::parseModuleName)
  AddModule ModuleTag_EMPDemo
    Draw = W3DModelDraw ModuleTag_01
      DefaultConditionState
        Model = None
      End
    End
    Body = ImmortalBody ModuleTag_02
      MaxHealth = 1.0
      InitialHealth = 1.0
    End
    Behavior = OCLUpdate ModuleTag_03
      OCL = SUPERWEAPON_EMPPulse
      CreateAtEdge = Yes
      MinDelay = 10000
      MaxDelay = 10000
    End
  End
End

Object EMPDemoPatriotBeacon
  EditorSorting = SYSTEM
  KindOf = NO_COLLIDE IMMOBILE UNATTACKABLE
  Geometry = SPHERE
  GeometryMajorRadius = 1.0
  GeometryIsSmall = Yes
  ; a map.ini may only add modules inside AddModule (ThingTemplate::parseModuleName)
  AddModule ModuleTag_EMPDemo
    Draw = W3DModelDraw ModuleTag_01
      DefaultConditionState
        Model = None
      End
    End
    Body = ImmortalBody ModuleTag_02
      MaxHealth = 1.0
      InitialHealth = 1.0
    End
    Behavior = OCLUpdate ModuleTag_03
      OCL = OCL_EMPDemoPatriotHit
      MinDelay = 4000
      MaxDelay = 4000
    End
  End
End
"""


# GlobalLighting of GC_SuperWeaponsGeneral (MapsZH.big), which the sandbox maps keep byte for byte:
# per time of day the terrain's and the objects' sun (ambient RGB, diffuse RGB, direction XYZ) and
# four unused extra lights.  Morning is the one the map plays in.
RETAIL_TIME_OF_DAY = 1
RETAIL_LIGHTING = [
    [(0.502, 0.392, 0.302, 0.902, 0.706, 0.600, -0.960, 0.050, -0.290),
     (0.502, 0.400, 0.302, 0.902, 0.702, 0.600, -0.960, 0.050, -0.290)],
    [(0.220, 0.204, 0.173, 1.000, 1.000, 1.000, -0.810, 0.380, -0.450),
     (0.220, 0.204, 0.173, 1.000, 1.000, 1.000, -0.810, 0.380, -0.450)],
    [(0.251, 0.231, 0.200, 0.600, 0.502, 0.400, -1.000, 0.000, -0.200),
     (0.251, 0.231, 0.200, 0.600, 0.502, 0.400, -1.000, 0.000, -0.200)],
    [(0.102, 0.102, 0.153, 0.200, 0.200, 0.302, -1.000, 1.000, -2.000),
     (0.102, 0.102, 0.153, 0.200, 0.200, 0.302, -1.000, 1.000, -2.000)],
]


def retail_lighting(data):
    """The fixture's GlobalLighting chunk, rewritten in place with RETAIL_LIGHTING."""
    names = {}
    count = struct.unpack_from("<i", data, 4)[0]
    offset = 8
    for _ in range(count):
        length = data[offset]
        name = bytes(data[offset + 1:offset + 1 + length]).decode("ascii")
        offset += 1 + length
        names[struct.unpack_from("<I", data, offset)[0]] = name
        offset += 4
    while offset < len(data):
        chunk_id, _, size = struct.unpack_from("<IHi", data, offset)
        body = offset + 10
        if names[chunk_id] == "GlobalLighting":
            out = struct.pack("<i", RETAIL_TIME_OF_DAY)
            for terrain, objects in RETAIL_LIGHTING:
                out += struct.pack("<9f", *terrain) + struct.pack("<9f", *objects)
                for _ in range(4):
                    out += struct.pack("<9f", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0)
            assert len(out) == size, (len(out), size)
            data[body:body + size] = out
            return data
        offset = body + size
    raise ValueError("no GlobalLighting chunk")


def write_object(writer, template, x, y, angle, unique):
    with writer.chunk("Object", 3):
        for value in (x, y, 0.0, angle):
            writer.real(value)
        writer.integer(0)
        writer.ascii(template)
        writer.dictionary({
            "objectInitialHealth": 100,
            "objectEnabled": True,
            "objectIndestructible": False,
            "objectUnsellable": False,
            "objectPowered": True,
            "objectRecruitableAI": False,
            "objectTargetable": False,
            "originalOwner": "teamPlyrCivilian",
            "uniqueID": unique,
        })


def build():
    # The fixture's own build, with our objects spliced into its ObjectsList.
    original_waypoint = generate_map.write_waypoint

    def waypoints_and_objects(writer, number, x, y):
        if number == 1:
            # The players' bases stay in their corners, away from the yard.
            original_waypoint(writer, 1, x, y)
            write_object(writer, "EMPDemoPulseBeacon", PULSE[0], PULSE[1], 0.0, "EMPDemoPulseBeacon 1")
            write_object(writer, "EMPDemoPatriotBeacon", PATRIOT[0], PATRIOT[1], 0.0, "EMPDemoPatriotBeacon 1")
            for index, (template, tx, ty, angle) in enumerate(TARGETS):
                write_object(writer, template, tx, ty, angle, "%s %d" % (template, index + 2))
        else:
            original_waypoint(writer, number, x, y)

    generate_map.write_waypoint = waypoints_and_objects
    try:
        return retail_lighting(generate_map.build_map(False))
    finally:
        generate_map.write_waypoint = original_waypoint


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    folder = Path(sys.argv[1]) / "Maps" / "EMPDemo"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "EMPDemo.map").write_bytes(build())
    (folder / "map.ini").write_text(MAP_INI.replace("\n", "\r\n"), newline="")
    print("Wrote %s/EMPDemo.map and map.ini" % folder)
    return 0


if __name__ == "__main__":
    sys.exit(main())
