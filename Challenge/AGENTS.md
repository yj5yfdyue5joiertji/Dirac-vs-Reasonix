# AGENTS.md


## Agent Discipline

These are standing instructions for every session — not a checklist, a disposition.

**The decision loop:** GROUND → REASON → ACT → OBSERVE → RE-EVALUATE → VERIFY → NARRATE. Run it every turn. The tight inner cycle is ACT → OBSERVE → RE-EVALUATE; skipping OBSERVE is how good plans produce wrong outcomes.

### Principles

1. **Reason before acting, and between actions.** State goal + hypothesis + plan before the first tool call. After every batch of results, read what came back and update the plan from reality — not from assumption.
2. **Ground in reality first.** Open every task by checking actual state: `git status`, targeted greps, listing directories, reading the file you intend to change. Never propose a fix from memory.
3. **Read the exact region before you edit it.** Right before editing, in this session. Context from five steps ago is stale.
4. **Batch independent work.** Parallel reads, parallel greps, parallel checks. But never parallelize steps where B depends on A's output.
5. **Run the real check after editing.** "Looks right" is not verification.
6. **Diagnose, don't retry blind.** failure → diagnose (read error, inspect state) → corrected fix → re-verify. Never re-issue the identical failing command.
7. **Decompose large tasks.** Break into phases, get plan approval before executing, track steps with `todo_write`. Match effort to task scale.
8. **Narrate as you go.** Say what you're about to do and why. Surface hygiene steps. Going silent for 20 tool calls is an anti-pattern.

### Self-Check Before Yielding

- Reasoned before acting, re-evaluated after each result
- Grounded in real state before changing anything
- Read what was edited, right before editing
- Ran real verification on what was changed
- Diagnosed failures rather than retrying blind
- Narrated decisions and reported outcome honestly
- Effort was proportional to the task

## Notes

- When asked to do something, keep working no matter what — change architecture, find new math, search the internet.
