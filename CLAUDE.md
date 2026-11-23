# OSDS

Unreal Engine 5.8 C++ driving simulator that builds the US from real data at runtime. Use the vocabulary in `GLOSSARY.md`; respect the decisions in `docs/adr/`.

## Where things are

- Code map, dev loop and vocabulary notes: `docs/team/how-the-game-works.md`, `docs/unreal-architecture.md`.
- Build, run, launch flags and console variables: `docs/unreal-building.md`.
- Current cycle plan (3.0): GitHub milestone `3.0`; bug list: `docs/release/bugs.md`.
- Expert reviewers: `.claude/agents/*.md` (read-only subagents; see `docs/team/README.md`).
- How work flows (grill, spec, tickets, one ticket per fresh session, `/code-review` on every PR, daily `/retro`, context and budget limits): `docs/agents/workflow.md`.
- Car data: `data/cars/<id>.json`, research per car in `lab/cars/*-research.md`.

## Checks (run these, never improvise editor commands)

- Build + headless tests: `powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Tier fast` (about 2 min; `-Tier full` before a merge; `-Filter OSDS.Vehicle` for one area).
- In-game drives: `scripts\preflight.ps1 -ShowWindow ...` (the Owner watches; never run game tests off-screen).
- One Unreal editor or GPU job at a time on this PC: the scripts take the locks in `scripts\EditorLock.ps1`.
- From bash (Git Bash), write script paths with forward slashes: `powershell -ExecutionPolicy Bypass -File scripts/check.ps1 -Tier fast`. Bash eats backslashes, and `\t` in a path becomes a tab.
- Run long builds and checks in the background and wait for the completion notice; never poll, and never keep a subagent alive just to wait for one.

## Rules

- Launch and look: before saying a change works, run the game, capture frames and open the images yourself.
- Every realism number names its source or is marked "(estimate)".
- It must work at a Random US place, not only on Hero roads.
- Never write the Owner's real name anywhere (code, docs, commits, issues, PRs).
- Legal data and licences only; no brand logos or badges.
- Work on a branch in your own Worktree; open a PR; the Coordinator reviews and merges.

## Agent skills

### Issue tracker

GitHub Issues on simcruisegame/osds, milestone `3.0`, area labels. See `docs/agents/issue-tracker.md`.

### Triage labels

The five default triage labels. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the root. See `docs/agents/domain.md`.
