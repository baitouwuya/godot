# Learnings

Capture corrections, knowledge gaps, best practices, and recurring patterns here.

Entry checklist:
- Use `LRN-YYYYMMDD-XXX`
- Include `Priority`, `Status`, and `Area`
- Add `Pattern-Key` and `See Also` when the pattern repeats

---

## [LRN-20260716-001] correction

**Logged**: 2026-07-16T17:48:51+08:00
**Priority**: medium
**Status**: resolved
**Area**: workflow

### Summary
Check mouse capture before injecting runtime input or interacting with an embedded visible game window.

### Details
ToonTest's `donut_walk` setup forces `MOUSE_MODE_CAPTURED`, and any mouse-button press captures the mouse again. Because a visible embedded game instance shares the editor window, trying to drag the window conflicts with mouse capture and recentering, which makes the window continue moving downward or resist being pulled back. The earlier `W` injection used a paired press and release, so there is no evidence that a stuck `W` caused this incident; injected held-state remains a general risk when release delivery is interrupted.

### Suggested Action
Before visible-instance input injection or window interaction, inspect and account for mouse capture. Prefer runtime property updates when the intent is to move a node. Extend the runtime input API with `tap`, `release_all`, and held-state cleanup so interrupted injections cannot leave controls pressed.

### Metadata
- Source: user_feedback
- Related Files: E:\Godot Projects\ToonTest\project
- Tags: godot, mcp, runtime-input, mouse-capture, embedded-game
- Pattern-Key: godot.runtime_input.mouse_capture_embedded_window
- Recurrence-Count: 1
- First-Seen: 2026-07-16
- Last-Seen: 2026-07-16

### Resolution
- Resolved: 2026-07-16T17:48:51+08:00
- Commit/PR: none
- Notes: Confirmed the capture-and-recenter conflict and separated it from the already released `W` input.

---

## [LRN-20260713-001] correction

**Logged**: 2026-07-13T14:29:19+08:00
**Priority**: medium
**Status**: resolved
**Area**: workflow

### Summary
Resolve the Godot project root from `project.godot` before launching the editor.

### Details
Passing `E:\Godot Projects\ToonTest` to `--path` opened the project manager because it is a container directory. The actual project marker is `E:\Godot Projects\ToonTest\project\project.godot`, so the project root is `E:\Godot Projects\ToonTest\project`.

### Suggested Action
Before every editor launch, locate and verify `project.godot`, then pass its parent directory to `--path`.

### Metadata
- Source: user_feedback
- Related Files: E:\Godot Projects\ToonTest\project\project.godot
- Tags: godot, launch, project-root, project-manager
- Pattern-Key: godot.launch.resolve_project_root
- Recurrence-Count: 1
- First-Seen: 2026-07-13
- Last-Seen: 2026-07-13

### Resolution
- Resolved: 2026-07-13T14:29:19+08:00
- Commit/PR: none
- Notes: Confirmed the project marker and corrected the launch path for subsequent editor runs.

---
