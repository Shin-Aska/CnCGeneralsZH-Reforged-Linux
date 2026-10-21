from __future__ import annotations

from pathlib import Path

from models import Platform, Scene


def build_command(
    scene: Scene,
    executable: Path,
    root: Path,
    overlay: Path | None,
    port: int,
    platform: Platform,
    *,
    map_path: str | None = None,
) -> list[str]:
    command = [
        str(executable), "-root", str(root), "-multiInstance", "-noaudio",
        "-win", "-xres", "1280", "-yres", "720", "-msaa", "0", "-quickstart",
        "-noshellmap", "-nologo", "-novideo", "-noFPSLimit", "-camera",
        str(scene.camera.x), str(scene.camera.y), "-map", map_path or scene.map_path, "-autoskirmish", "2",
        "-seed", "1", "-side", "0", "FactionAmerica", "-side", "1", "FactionChina",
        "-observer", "-maxframes", str(scene.max_frames),
        "-logPrefix", f"water_{scene.scene_id}_",
    ]
    if overlay is not None:
        command[3:3] = ["-overlay", str(overlay)]
    if platform is not Platform.WINDOWS:
        command.insert(3, "-offscreen")
    if scene.requires_motion:
        command.extend(("-control", str(port)))
        start = max(1, scene.screenshot_frame - 120)
        command.extend(("-video", str(start), str(scene.screenshot_frame), scene.scene_id, "-recordfps", "60"))
    else:
        command.extend(("-turbo", "-screenshot", str(scene.screenshot_frame)))
    if scene.scenario is not None:
        command.extend(("-takeover", "-scenario", scene.scenario))
    return command
