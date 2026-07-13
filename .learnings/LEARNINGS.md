# Learnings

Capture corrections, knowledge gaps, best practices, and recurring patterns here.

Entry checklist:
- Use `LRN-YYYYMMDD-XXX`
- Include `Priority`, `Status`, and `Area`
- Add `Pattern-Key` and `See Also` when the pattern repeats

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
