# CLI Performance Recorder

Branch-local custom feature for this Godot 4.6 repository. This page is the
single durable reference for the CLI performance recorder and stays outside the
official `doc/` tree on purpose.

## Summary

This feature adds a CLI-only profiling path for project runs that enter the
main loop.

It records per-frame timing plus numeric `Performance` monitors, then prints a
machine-readable JSON summary as the last line on stdout.

## Quick Start

If you only need to use the feature, start here.

Basic summary for a project run:

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --path "E:\Godot Projects\mysterious-museum" `
  --fixed-fps 60 `
  --quit-after 30 `
  --perf-record `
  --perf-end-frame 29
```

Keep the worst frames in the summary:

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --path "E:\Godot Projects\mysterious-museum" `
  --fixed-fps 60 `
  --quit-after 30 `
  --perf-record `
  --perf-end-frame 29 `
  --perf-top-frames 5
```

Also export per-frame JSONL samples:

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --headless `
  --path "E:\Godot Projects\rush-pet" `
  --fixed-fps 60 `
  --quit-after 30 `
  --perf-record `
  --perf-end-frame 59 `
  --perf-samples-file "E:\GitHub\godot\.omx\rushpet_perf_samples.jsonl"
```

## Common Tasks

Use these patterns most often:

- quick health snapshot
  `--perf-record --perf-end-frame 59`
- spike hunting
  `--perf-record --perf-end-frame 119 --perf-top-frames 10`
- summary plus raw samples
  `--perf-record --perf-end-frame 119 --perf-samples-file <path>`
- benchmark coexistence
  `--benchmark --perf-record --perf-end-frame <n>`

## CLI Surface

### Flags

- `--perf-record`
- `--perf-start-frame <int>`
  Default `0`.
- `--perf-end-frame <int>`
  Required when recording is enabled.
- `--perf-top-frames <int>`
  Default `10`. `0` disables `slowFrames`.
- `--perf-samples-file <path>`
  Opt-in JSONL export for per-frame samples.

### Mode limits

- supported: CLI project runs that enter the main loop
- not supported: editor, project manager, LSP CLI, import/export, or other
  one-shot command-line tools

### Top-level summary shape

- `recording`
- `frameMetrics`
- `builtinMonitorMetrics`
- `customMonitorMetrics`
- `budgetSummary`
- `slowFrames`
  Present unless `--perf-top-frames 0`.

### Per-metric fields

Each metric summary includes:

- `count`
- `min`
- `max`
- `mean`
- `variance`
- `stddev`
- `first`
- `last`
- `delta`
- `p50`
- `p95`
- `p99`

### Frame metrics

The fixed per-frame metrics are:

- `frame_time_usec`
- `process_time_usec`
- `physics_process_time_usec`
- `navigation_process_time_usec`
- `physics_frame_time_sec`
- `fps`

### Budget summary

`budgetSummary` tracks frame-time overruns against fixed thresholds:

- `over16_667ms`
- `over33_333ms`
- `over50_000ms`

Each entry includes:

- `thresholdMs`
- `overCount`
- `overRatio`

### Slow frames

`slowFrames` is sorted from slowest to faster by `frame_time_usec`.

Each item includes:

- `frame`
- `frameMetrics`
- `builtinMonitors`
- `customMonitors`

### JSONL sample shape

`--perf-samples-file` writes one JSON object per sampled frame with:

- `frame`
- `frameMetrics`
- `builtinMonitors`
- `customMonitors`
- `recorded`

## Reading the Results

Read this section when the summary is not enough by itself.

### What the percentiles add

- `mean` tells you average cost
- `p50` tells you the typical frame
- `p95` and `p99` tell you whether spikes dominate the experience

### What `slowFrames` adds

`slowFrames` is the fastest way to inspect the worst moments without opening a
full trace.

Use it when:

- `mean` looks fine but play feels uneven
- `p95` or `p99` is much worse than `p50`
- you need to compare built-in monitor values at spike frames

### What `budgetSummary` adds

The fixed thresholds map to practical frame budgets:

- `16.667ms` is roughly 60 FPS
- `33.333ms` is roughly 30 FPS
- `50.000ms` is roughly 20 FPS

Use `overRatio` as the fastest coarse signal for whether frame pacing is
consistently missing a target budget.

### When to use the samples file

Use `--perf-samples-file` when the summary shows a problem but you still need:

- full frame-by-frame correlation
- external plotting
- custom offline analysis

## Validation

Read this section when you need confidence, not just usage.

### Build and unit checks

Validated on this branch with:

```powershell
scons platform=windows target=editor dev_build=yes module_mono_enabled=no tests=yes d3d12=no -j2
```

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe `
  --test `
  --test-suite="[Main][CLIPerformanceRecorder]"
```

Observed result:

- `7 passed`
- `0 failed`

### Manual smoke checks

Validated with:

- `E:\Godot Projects\rush-pet`
  `--headless`, `--benchmark`, `--perf-samples-file`, early exit before
  `end-frame`, `completed=false`
- `E:\Godot Projects\mysterious-museum`
  non-`headless`, `completed=true`, `slowFrames` populated, stdout last line
  remained JSON

Observed stable outcomes:

- summary JSON is emitted on stdout
- `budgetSummary` and `slowFrames` are present
- JSONL sample export line count matches captured frames
- `--benchmark` output still appears before the summary JSON

## Known Limits

- The summary is strong for detection and coarse localization, but it is still
  not a full trace viewer.
- `customMonitorMetrics` only includes numeric custom monitors.
- Project warnings or leak reports may still appear on stderr after the stdout
  summary, so machine parsers should read stdout only.

## Related

- [../index.md](../index.md)
- [../log.md](../log.md)
