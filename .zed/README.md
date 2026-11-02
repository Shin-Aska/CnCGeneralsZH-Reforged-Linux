# Zed launch configurations

These files mirror `.vscode/launch.json` and its supporting `.vscode/tasks.json`
using Zed's native `.zed` directory.

- Run **debugger: start** (F4) for **Zero Hour: Play (Linux)** or
  **Zero Hour: AI skirmish (Linux)**. These use Zed's GDB adapter, which requires
  GDB 14.1 or newer built with Python/DAP support and available on `PATH`.
- Run **task: spawn** for the four terminal-only launch profiles: **Publish thin
  executable (only)**, **Publish and Play (thin executable)**, **Play (no debugger)**,
  and **AI skirmish (no debugger)**, all prefixed with **Zero Hour:**.
  The supporting Linux build, publish, and portable-release tasks are also available.

The debug profiles reference the release build task. Terminal-only tasks chain
prerequisites with `&&` because Zed tasks do not use VS Code's `dependsOn` field;
failed builds or publishes therefore prevent the next step from running.
Compiler overrides remain `CC=gcc-16` and `CXX=g++-16` for builds.

The game arguments and user-data location are unchanged. In particular,
`build-linux/vscode-user` is intentionally shared with VS Code so switching
editors does not reset game settings. Thin publishing still targets
`$HOME/Games/ZeroHourReforged`.

The GDB adapter uses native DAP rather than VS Code's MI adapter, so the MI-only
`-enable-pretty-printing` setup command is not passed to it. The launch profiles
continue without stopping at entry. Release builds are retained for parity with
VS Code; source-level debugging requires a binary built with debug information.
