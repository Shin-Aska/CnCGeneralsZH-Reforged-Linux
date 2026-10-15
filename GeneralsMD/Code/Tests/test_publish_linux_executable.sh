#!/usr/bin/env bash
# Copyright 2026. Additional terms under GNU GPL section 7 apply: see LICENSE.md.
# Exercise thin publishing without a game build, installed game, or external dependencies.
set -euo pipefail
[ "$(uname -s)" = Linux ] || { echo "skip: not Linux"; exit 77; }
code="$(cd "$(dirname "$0")/.." && pwd)"
temporary=$(mktemp -d "${TMPDIR:-/tmp}/test-publish-linux.XXXXXX")
trap 'rm -rf -- "$temporary"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
fixture="$temporary/source tree/Code"
package="$temporary/installed game"
overlay="$package/share/zero-hour-reforged/overlay"
mkdir -p "$fixture/Tools" "$fixture/Data/INI" "$fixture/Data/Window" \
  "$package/bin" "$overlay/Data/INI" "$overlay/Data/Custom" "$overlay/Window" "$overlay/Art/Custom"
cp "$code/Tools/publish-linux-executable.sh" "$code/Tools/stage-overlay.sh" "$fixture/Tools/"
printf 'Weapon NewWeapon\nEnd\n' > "$fixture/Data/INI/WeaponReforged.ini"
printf 'ControlBarScheme NewScheme\nEnd\n' > "$fixture/Data/INI/ControlBarProReforged.ini"
printf 'current window\n' > "$fixture/Data/Window/Current.wnd"
printf 'obsolete window\n' > "$overlay/Window/Removed.wnd"
printf 'obsolete ini\n' > "$overlay/Data/INI/Removed.ini"
printf 'custom data\n' > "$overlay/Data/Custom/keep.txt"
printf 'custom art\n' > "$overlay/Art/Custom/keep.txt"
printf 'installed art archive\n' > "$overlay/ReforgedTest.big"
printf 'custom root file\n' > "$overlay/keep.txt"
ln -s ReforgedTest.big "$overlay/ReforgedLinked.big"
cp /bin/false "$package/bin/generals"
cp /bin/true "$temporary/built-generals"
cp "$package/bin/generals" "$temporary/previous-generals"
cp "$temporary/built-generals" "$temporary/source-before"
publisher="$fixture/Tools/publish-linux-executable.sh"

bash "$publisher" "$temporary/built-generals" "$package/bin/generals" > "$temporary/publish.log" 2>&1 \
  || { cat "$temporary/publish.log"; fail "publish failed"; }
"$package/bin/generals" || fail "new executable does not run"
cmp "$temporary/built-generals" "$temporary/source-before" || fail "build executable changed"
for name in WeaponReforged.ini ControlBarProReforged.ini; do
  [ -f "$overlay/Data/INI/$name" ] || fail "required $name was not published"
  perl -e 'local $/; $_=<>; exit(/(?<!\r)\n/ ? 1 : 0)' "$overlay/Data/INI/$name" \
    || fail "$name did not receive packaged CRLF line endings"
done
[ -f "$overlay/Window/Current.wnd" ] || fail "current window absent"
[ ! -e "$overlay/Window/Removed.wnd" ] && [ ! -e "$overlay/Data/INI/Removed.ini" ] \
  || fail "obsolete managed files survived"
[ "$(cat "$overlay/ReforgedTest.big")" = 'installed art archive' ] \
  && [ "$(readlink "$overlay/ReforgedLinked.big")" = ReforgedTest.big ] \
  && [ "$(cat "$overlay/Data/Custom/keep.txt")" = 'custom data' ] \
  && [ "$(cat "$overlay/Art/Custom/keep.txt")" = 'custom art' ] \
  && [ "$(cat "$overlay/keep.txt")" = 'custom root file' ] \
  || fail "unrelated files or art changed"
backups=("$package"/.thin-publish-backup.*)
[ "${#backups[@]}" -eq 1 ] && [ -d "${backups[0]}" ] || fail "missing recovery directory"
cmp "$temporary/previous-generals" "${backups[0]}/previous/bin/generals" || fail "previous executable not recoverable"
[ -f "${backups[0]}/previous/share/zero-hour-reforged/overlay/Window/Removed.wnd" ] \
  || fail "previous overlay not recoverable"
echo "ok: executable and required runtime data publish together; obsolete files removed, custom files/art preserved, recovery retained"

# A real staging error: stage-overlay.sh cannot normalize a missing INI directory. Neither the
# installed package nor its successful-publish backup may change when preparation fails.
cp -a "$package" "$temporary/expected-package"
mv "$fixture/Data/INI" "$fixture/Data/INI-unavailable"
if bash "$publisher" "$temporary/built-generals" "$package/bin/generals" > "$temporary/staging-failure.log" 2>&1; then
  fail "missing source INI did not fail staging"
fi
diff -r --no-dereference "$temporary/expected-package" "$package" || fail "failed staging changed installed files"
mv "$fixture/Data/INI-unavailable" "$fixture/Data/INI"
echo "ok: failed staging leaves the executable, overlay, and recovery backup unchanged"

# Fail after overlay swaps, when the new executable is moved into place. The old executable and
# the entire old managed overlay must be restored, including files removed by this new publish.
mkdir -p "$temporary/failing-tools"
real_mv=$(command -v mv)
cat > "$temporary/failing-tools/mv" <<'WRAPPER'
#!/usr/bin/env bash
for argument in "$@"; do
  case "$argument" in */next/bin/generals) echo 'injected executable replacement failure' >&2; exit 1;; esac
done
exec "$PUBLISH_TEST_REAL_MV" "$@"
WRAPPER
chmod +x "$temporary/failing-tools/mv"
printf 'changed window\n' > "$fixture/Data/Window/Current.wnd"
rm "$fixture/Data/INI/WeaponReforged.ini"
if PATH="$temporary/failing-tools:$PATH" PUBLISH_TEST_REAL_MV="$real_mv" \
  bash "$publisher" "$temporary/built-generals" "$package/bin/generals" > "$temporary/replacement-failure.log" 2>&1; then
  fail "injected replacement error did not fail publishing"
fi
diff -r --no-dereference "$temporary/expected-package" "$package" || fail "failed replacement did not restore installed files"
echo "ok: failed executable replacement rolls the executable and runtime overlay back together"
