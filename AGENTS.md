# VT100_Circle Agent Constitution

This file defines standing rules for **all AI coding agents** working in this repository.
These rules are mandatory unless the user explicitly overrides a specific rule.

## 1) Rule Priority

1. User safety/legal constraints
2. This `AGENTS.md`
3. Task-specific user request
4. Agent defaults

If rules conflict, follow the higher-priority rule and state the conflict briefly.

## 2) Scope and Boundaries

- Primary app scope: `VT100/`
- Framework/library scope: `include/`, `lib/`, `addon/`, `boot/`, `tools/` only when required by the task.
- Do **not** modify generated artifacts unless explicitly requested:
  - `VT100/build/`
  - `VT100/docs/doxygen/`
  - `doc/html/`

## 3) Mandatory Planning Workflow (Always)

Before making any code or documentation changes, the agent **MUST**:

1. Provide an **incremental plan** with small numbered steps.
2. Mark expected impact/risk per step (low/medium/high).
3. Ask for explicit approval.
4. Wait for approval before executing edits.

Approval tokens accepted: `approved`, `go`, `proceed`, `yes`.

Without approval, agent may only do read-only analysis.

## 4) Mandatory Backup Before Massive Refactoring

Before any massive refactoring, the agent **MUST** create a backup snapshot.

Massive refactoring means any of:

- Touching more than 5 files, or
- Moving/renaming files or directories, or
- Changing public interfaces used across modules, or
- Rewriting core modules (`kernel`, renderer, parser, config, setup, UART, WLAN).

Backup requirements:

- Location: `VT100/backups/refactor-YYYYMMDD-HHMMSS/`
- Include all files intended for modification.
- Include `MANIFEST.md` with:
  - reason for refactor
  - file list
  - restore instructions
  - git commit hash (if available)

Agent must report backup path before applying refactor edits.

## 5) Implementation Rules

- Keep changes minimal and task-focused.
- Preserve existing behavior unless change request says otherwise.
- Avoid stylistic churn in unrelated files.
- Do not add new dependencies unless necessary and approved.
- Do not commit, create branches, or rewrite history unless explicitly requested.

## 6) Validation Rules

After changes, run targeted validation first, then broader checks:

1. `make docs` in `VT100/` when docs/config/Doxygen changed
2. `make -j4` in `VT100/` when code/build files changed

If a check fails, summarize root cause and proposed fix before further broad changes.

## 7) Response Format Rules

Final handoff should include:

- What changed
- Validation run and result
- Whether today's `VT100/DAILY_CHANGELOG.md` entry was updated
- Any remaining risks/open points
- Next suggested step

Keep status updates concise and actionable.

## 8) Daily Change Log Requirement

Agents **MUST** maintain a daily log file at:

- `VT100/DAILY_CHANGELOG.md`

For each day with implemented work, append a dated entry that summarizes:

- implemented features
- codebase changes

Use one section per date in ISO format (`YYYY-MM-DD`) and keep entries concise.

## 9) Commit Request Workflow After Successful Validation

After implementing a feature or correcting a bug and completing relevant validation:

- If tests are manual or user-run, the agent **MUST** ask the user whether the test was successful.
- If validation is successful (agent-run or user-confirmed), the agent **MUST** draft a commit message.
- The agent **MUST** request explicit user approval before creating a commit.
- Without explicit approval, the agent must not commit.
- Exception: if a task changes only `VT100/DAILY_CHANGELOG.md`, the agent **MUST NOT** request a commit.

## 10) Definition of Done

A task is considered done only when all applicable items are complete:

- Requested implementation is complete and scope-limited.
- Relevant validation has been executed and reported.
- Today's `VT100/DAILY_CHANGELOG.md` entry has been updated (or explicitly stated as not applicable).
- Remaining risks/open points are documented.
- Next suggested step is provided.

## 11) Validation Depth and Escalation

Validation should proceed from narrow to broad:

1. Run the most targeted check(s) for the modified area first.
2. Run broader project checks only when justified by the change scope.

Escalation rule:

- If relevant validation fails twice, the agent should stop broadening changes and report:
  - likely root cause
  - minimal corrective options
  - recommended next step for user decision

## 12) Commit Message Draft Standard

When drafting a commit message after successful validation, use this structure:

- Subject: imperative, concise, and scoped (example: `VT100: fix cursor redraw on line insert`).
- Body:
  - what changed
  - why it changed
  - validation performed

The agent must present this draft and request approval before creating a commit.

## 13) Dependency and Security Impact Note

If a change adds or updates dependencies, networking behavior, authentication, or configuration affecting runtime exposure, final handoff must include:

- dependency/license impact (if any)
- security/runtime exposure impact (if any)
- rollback note (how to revert safely)
