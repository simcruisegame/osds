# How agents work in this repo (Matt Pocock's skills, as intended)

This is the result of the 2026-11-23 retrospective. Read [ask-matt](https://www.aihero.dev/skills-ask-matt) for the map of the skills.

## The flow

1. **Grill.** The Owner types `/grill-with-docs`, the agent asks, and the **Owner answers**: decisions are the Owner's, facts are the agent's. Every settled term goes into `GLOSSARY.md`, and every hard-to-reverse decision into `docs/adr/`. An agent never answers its own grilling questions. **Exception (Owner, 2026-11-23):** the Owner has delegated the Owner's role to the Coordinator session for grills, ticket approvals and the daily retro. When standing in, the Coordinator must first open fresh Ultra frames of the area (and get an `art-director` review) and ground each answer in what the frames show, so that what a player would notice comes from the frames rather than from the Owner pointing it out later.
2. **Spec.** `/to-spec`, typed in the same context window as the grill, publishes to GitHub after agreeing the test seams.
3. **Tickets.** `/to-tickets #<spec>`, typed in the same window. The Owner approves the breakdown at its quiz step. Tickets are never filed by hand: hand-filed tickets lose their blocking edges and their sizing.
4. **Build.** **One ticket, one fresh session.** That session's first message is `/implement #<ticket>`. A ticket heading past about 100k tokens is too big: stop and split it.
5. **Review.** Every PR gets `/code-review <base>` (Standards and Spec as independent, fresh subagents) before merge. A thread's own test claims are not review.
6. **PR.** Use the `/pr` body: a summary visual, real before/after evidence (frames at Ultra and frame times), and the merge-danger call.
7. **Retro.** At the end of each day, the Owner types `/retro` in the coordinator session. Every mechanical mistake becomes a deterministic check (a script, a test or a hook), not a note.

## Context and budget

- The smart zone is about 150k tokens. Compact at a phase boundary at about 120-150k (`/compact <what's next>`). Use `/handoff` only when moving to a new directory or a colleague. The hard ceiling is 200k for everyone, including the coordinator.
- **At most 2 work sessions at once.** This PC has one build lock and one GPU, so extra sessions mostly wait and burn the 5-hour usage limit. Only the frontier of tickets (those with no open blockers) runs.
- Builds and tests run in the background with a completion notice. Never poll, and never keep an agent alive just to wait.
- A huge, foggy effort goes through `/wayfinder` first. Use `/improve-codebase-architecture` when a ticket is hard to test.

## Feedback loops first

Before trusting any test result, check that the harness measures what it claims to:
- frame times are recorded;
- runs happen at the target resolution;
- launches don't hang.

A test that can't fail is worse than none.
