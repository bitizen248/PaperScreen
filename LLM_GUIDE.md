# LLM_GUIDE.md

Practical instructions for Codex, Junie, and similar assistants.

## Default assumptions

- This is a firmware project for the LilyGO T5 E-Paper S3 Pro.[cite:17][cite:18]
- The product is a portable productivity tool.
- Primary features are tasks, reader, timers, and optional desk-board mode.
- The UI is e-paper-first, not phone-first.
- Battery life and reliable wake behavior matter.

## Code generation rules

Always prefer:
- small patches
- explicit interfaces
- deterministic state transitions
- simple structs and enums
- module-local reasoning

Never assume:
- generic ESP32 sample code will fit this board
- a random Arduino display library is compatible
- continuous animation is acceptable
- pin mappings from another LilyGO board are valid

## Prompt template

Use this template when asking an LLM for code:

```text
You are editing a firmware repository for the LilyGO T5 E-Paper S3 Pro.
Constraints:
- Do not invent hardware details.
- Keep the architecture layered: board, display, services, ui.
- Do not let UI code own business state.
- Optimize for e-paper refresh behavior.
- Return the smallest safe patch.
Task:
<describe one narrow task>
Files allowed to change:
<list>
Files that must not change:
<list>
```

## Good tasks for an LLM

- Add a task sorting function
- Add a timer state enum
- Add a view model for the home screen
- Add SD-backed settings persistence
- Refactor a screen to consume a service interface
- Add a compact desk-board layout config

## Bad tasks for an LLM

- Rewrite the entire firmware
- Replace the board support stack
- Change all display abstractions at once
- Invent a new cross-platform framework
- Add networking, sync, and UI refactors in one patch

## Review checklist for generated code

- Does it keep hardware-specific logic contained?
- Does it avoid guessed constants?
- Does it preserve e-paper-friendly refresh behavior?
- Does it separate state from rendering?
- Is the public interface smaller, not bigger?
- Could a human debug it quickly?

## Preferred answer style from an LLM

Ask the model to return:
- a short explanation
- exact file diffs or full file replacements for only the touched files
- any follow-up assumptions clearly labeled
- no unrelated refactors
