

## Modern water contribution

On Linux, Water style selects Modern or Classic live. Modern is the default.
Windows keeps its original water shader identities; macOS uses the legacy SDL
water programs and hides the Linux selector. Material rules are loaded from
`Data/INI/ModernWater.ini` through the game filesystem. Source edits are staged
into the build overlay; invalid or missing game rules use built-in defaults.

Build the standalone editor explicitly: `cmake --build build-linux --target water_preview`.
See [water-preview.md](water-preview.md) for controls, matching, explicit exports
and the distinction between game fallback and preview parse failures.

The reference baseline fails `widechar_check` on hotkey character literals and a
tooltip arrow. The prerequisite text fixes use `u` character literals and a
`\u2192` escape, retaining the same character values while satisfying the
portable source checks. These fixes do not enable new Windows rendering.

## Linux sun shadows

Sun maps are the normal Linux path. The existing shadow preferences still apply.
For validation, `ZH_SUN_SHADOW_MODE=legacy`, `capture-only` and `mapped` select
fallback, capture-only and receiving passes. Unknown values retain fallback.
See [sdl-sun-shadows.md](sdl-sun-shadows.md) for the scene harness and diagnostics.
Windows/macOS compilation and runtime remain unverified for this proposal.
