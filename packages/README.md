# Shared Dart packages

Not scaffolded yet — they arrive with their own planning document, as PLAN.md sets out. Per that
layout:

| Package | What | Flutter? |
| --- | --- | --- |
| `rct_core` | `.rcl` format, geometry, track and lap analysis — the Dart counterpart of `firmware/lib/` | no, pure Dart |
| `rct_charts` | linked-cursor `CustomPainter` charts | yes |
| `rct_ui` | shared widgets, theme, map layers | yes |

They will be **native pub workspace** members (`workspace:` in the root `pubspec.yaml`,
`resolution: workspace` in each package) rather than melos, giving one lockfile and one
`.dart_tool`. The root `pubspec.yaml` is created together with the first member, since a workspace
listing members that do not exist does not resolve.

`rct_core` and `firmware/lib/{rcl,geo}` are two implementations of one specification
([docs/protocol.md](../docs/protocol.md), [docs/algorithms.md](../docs/algorithms.md)) and are held
in agreement by the shared fixtures in [docs/fixtures/](../docs/fixtures/).
