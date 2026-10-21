from __future__ import annotations
import struct


def bmp(mode: str) -> bytes:
    pixels = bytes((40,100,200) if mode == "blank" else (200,100,40))*16 + bytes((255,255,255))*16
    if mode == "blank":
        pixels = bytes(96)
    header = b"BM"+struct.pack("<IHHI",150,0,0,54)+struct.pack("<IiiHHIIiiII",40,8,4,1,24,0,96,0,0,0,0)
    data = header+pixels
    return (b"XX"+data[2:]) if mode == "malformed" else data


def group(logic: int, match_id: int, oid: int, did: int, present: bool, qo: int, qd: int, action: str) -> str:
    found, drawn = int(present and qo == oid),int(present and qd == did)
    text = action+f"water-capture-inspect: logic={logic} client={logic} query-object={qo} object-found={found} object-destroyed={0 if found else -1} query-drawable={qd} drawable-found={drawn} named-found={int(present)} game-birth=UNKNOWN object-birth=UNKNOWN drawable-birth=UNKNOWN current-view=UNKNOWN\n"
    if present:
        text += f"water-capture-inspect-owner: name=WaterFixtureLow object={oid} destroyed=0 dead=0 player=2 template=WaterCaptureCarrier drawable={did} drawable-found=1 x=760 y=280 z=29 hidden=0 shrouded=0\n"
        # Rest-only inspector snapshot: later Wave must come from the exact live canonical owner row.
        text += f"water-capture-inspect-system: system={100+match_id} attached-object=0 attached-drawable={did} system-start-frame=5 game-birth=UNKNOWN native-issue=UNKNOWN\n"
    return text+f"water-capture-inspect-systems: manager=1 matched={int(present)} reported={int(present)} truncated=0\n"


def render(logic: int, frame: int, oid: int, did: int, match_id: int) -> str:
    capture = frame-90
    key = f"frame={frame} generation={match_id} capture={capture} colour=123 depth=456 view={capture} target=(8,4,10,1)"
    text = f"water-device: frame={frame} patch=17 software=0 swapchain=1\nwater-ownership: frame={frame} generation={match_id} capture={capture} patch=17 ownership=3 vessel-y=1 atlas-level=25 water-level=25 channels=7 mirror=0\nwater-route: event=issued attempt=1 frame={frame} patch=17 capture={capture} commands=1 batches=1 coverage=1 ownership-eligible=1 admissions-before=0 admissions-after=1 black=000001 white=000002\n"
    for index,(template,texture,sid) in enumerate((("AmphibWaveRest","EXShockWav.tga",100+match_id),("AmphibWave","EXWave01.tga",200+match_id))):
        text += f"water-particle-observe: phase=main-decision template={template} texture={texture} system={sid} owner={oid} drawable={did} object=0 logic={logic} render={frame-1} mirror=0 ordinary=1 particles=2 built=0 skipped=1 admitted=1 view-before=1 view-after=1 view-stable=1 {key} owner-position=(760,280,29) water-at-owner=25 stats-sampled=0 stats-active=(0,0) stats-render=(0,0) pipelines=(0,0) draws-made=(0,0) draws-refused=(0,0) native-rs-query=0 native-rs-fill=UNKNOWN native-rs-enum=0 record={(frame-101)*2+index+1}/512\n"
    return text
