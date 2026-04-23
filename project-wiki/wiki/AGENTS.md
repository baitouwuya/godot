# Project Wiki Schema

This `AGENTS.md` governs the `project-wiki/wiki/` subtree.

## Purpose

Maintain branch-local customization knowledge without mixing it into Godot's
official `doc/` tree.

## Core rules

- Keep durable synthesis in `wiki/`.
- Treat `../raw/` as the place for immutable source material if source bundles
  need to be archived later.
- Prefer one stable page per durable custom feature instead of splitting a
  small feature into many near-duplicate notes.
- Update `index.md` and `log.md` whenever the wiki changes materially.
- Use relative Markdown links inside the wiki.

## Page shape

- Put the shortest useful summary near the top.
- Reveal operational usage before implementation details.
- Put debugging, testing, and known limits after the user-facing sections.
- End durable pages with `## Related`.
