# Custom Feature Branching

Branch-local workflow for custom Godot development in this repository.

## Summary

Each custom feature must be developed on its own branch first.

Only after the feature is complete and verified should it be merged into the
custom mainline branch `codex/custom-main`.

## Workflow

Use this sequence for every new custom feature:

1. Start from `codex/custom-main`.
2. Create a dedicated feature branch.
3. Implement and verify the feature on that branch.
4. Commit and push the feature branch.
5. Merge the finished feature branch back into `codex/custom-main`.
6. Push `codex/custom-main`.

Preferred branch naming:

- `codex/<feature-slug>`

Examples:

- `codex/lsp-cli`
- `codex/cli-perf-recorder-v2`

## Daily Rules

- Do not develop multiple custom features directly on `codex/custom-main`.
- Keep one durable feature per feature branch.
- If a feature needs follow-up work that is still part of the same deliverable,
  continue on the same feature branch until that deliverable is complete.
- If the work is a new custom capability, start a new feature branch from the
  latest `codex/custom-main`.

## Merge Rules

- Merge into `codex/custom-main` only after build, test, and target project
  validation are done for that feature.
- Prefer a clean fast-forward merge when the branch history allows it.
- Push the feature branch before merging so the standalone branch remains a
  durable record of that custom feature.

## Checklist

Before merging a custom feature into `codex/custom-main`, confirm:

- code changes are committed on the feature branch
- required build and test checks passed
- the target Godot project scenarios were manually validated when needed
- branch-local wiki docs were updated if the feature changed CLI, workflow, or
  maintenance expectations

## Known Limit

This workflow keeps feature history clean, but it still depends on disciplined
branch scoping. If unrelated work is mixed into one feature branch, the merge
will still carry that coupling into `codex/custom-main`.

## Related

- [../index.md](../index.md)
- [../log.md](../log.md)
