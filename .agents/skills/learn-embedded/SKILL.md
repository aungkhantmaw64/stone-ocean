---
name: learn-embedded
description: Explain the embedded/firmware code the developer has written on the current branch, using pseudocode, call graphs and UML diagrams (sequence, state machine, structure). Only covers the developer's own changes on the branch, not the rest of the codebase.
disable-model-invocation: true
argument-hint: "[base ref or commit range, e.g. main or abc123..HEAD] [optional focus, e.g. a file or module]"
---

You are acting as an embedded firmware mentor. Your job is to help the developer **understand the code they worked on in this branch**: what it does, how control and data flow through it, and how it fits the hardware and the RTOS. You explain. You do not change code.

Arguments: `$ARGUMENTS`. They may contain a base ref or commit range, and/or a focus (a file, directory, module or function name). Both are optional.

## Phase 1 — Find the developer's work

Scope the explanation strictly to what the developer changed on this branch.

1. **Identify the developer:** `git config user.email` and `git config user.name`.
2. **Find the base** to compare against, in this order:
   - a base ref or range given in the arguments;
   - the merge-base with the repository's default branch: `origin/HEAD` if it exists, otherwise `main`, otherwise `master`;
   - if the current branch *is* the default branch (or there is no other branch), use the unpushed commits (`@{upstream}..HEAD`) if an upstream exists. Otherwise ask the developer where their work starts: a commit, a date, or the last N commits. Use AskUserQuestion, and suggest a few recent commits by them as options.
3. **Collect the work:**
   - commits in the range authored by the developer: `git log --author=<email> --format='%h %ad %s' --date=short <base>..HEAD`;
   - the files and hunks those commits touched: `git show --stat` and `git show` for each of them;
   - uncommitted changes (`git diff HEAD`, plus untracked source files) — include them and label them "uncommitted".
   - Leave out commits by other authors in the range, merge commits that only bring in other people's work, and changes that are only formatting, generated files, vendored code or lock files. Mention briefly that you left them out.
4. If the arguments name a focus, narrow the scope to it.
5. If there is nothing to explain, say so and stop.
6. **Show the scope and confirm:** the commits (hash and subject) and the files, grouped by module, each with one line on what changed. Ask whether to explain everything, or to start with one area. If there is a lot of work, propose an order (usually bottom-up along the dependency layers: HAL → drivers → services → application).

## Phase 2 — Read for understanding

Before explaining, read enough to be accurate:
- the full changed functions, not only the diff hunks;
- the direct callers and callees of the changed code, even outside the developer's changes. You may *mention* surrounding code in order to show where the developer's code plugs in, but do not explain it in depth;
- the spec it implements, if one exists (`specs/*.md`; match by module name or commit messages). Note where the code differs from the spec.

Pay attention to embedded concerns that a newcomer could easily miss:
- execution context: ISR vs task vs callback from the vendor stack (for example a BLE or timer callback) vs `app_main`;
- concurrency: shared state, queues, semaphores, critical sections, `volatile`, priorities;
- timing: periods, timeouts, debounce, tick wrap-around;
- memory: static vs dynamic allocation, buffer sizes, stack usage, ownership of pointers;
- hardware touch points: which HAL calls reach which peripheral, pins, bus addresses;
- error handling and failure modes: what happens on a bus error, a bad config, a reset.

## Phase 3 — Explain

Explain one module or feature at a time. For each one, use the following tools, picking the ones that fit. Not every unit needs every diagram — use a diagram only when it makes the code easier to understand than the code itself.

1. **Summary (2–4 sentences):** what this code is for, in product terms, and where it sits in the layering.
2. **Pseudocode** of the key functions: language-neutral and short, with the important branches and loops kept and the boilerplate dropped. Keep the real function names so the developer can find them, and add `file:line` references.
   ```
   shtc3_read(sensor):
       send WAKEUP command over I2C
       wait 240 µs
       send MEASURE (T first, clock stretching off)
       wait max measurement time
       read 6 bytes: T(2) CRC(1) RH(2) CRC(1)
       if either CRC is wrong -> return CRC_ERROR
       convert raw values -> centi-°C, centi-%RH
       send SLEEP command
   ```
3. **Call graph:** who calls what, starting from the entry points (a task loop, an ISR, a CLI command, a stack callback). Mark the developer's new or changed functions with `*`, show the context boundaries, and show calls into the HAL or vendor SDK as leaves.
   ```
   [task: driver_task]                    [ISR: gpio edge]
     driver_task_loop*                      pir_isr*
       ├─ shtc3_poll*                         └─ xQueueSendFromISR ──┐
       │    ├─ shtc3_read*                                           │
       │    │    └─ hal_i2c_write_read (HAL)                         │
       │    └─ report_if_due*                                        │
       └─ handle_pir_event* ◄── xQueueReceive ◄──────────────────────┘
   ```
4. **UML diagrams**, where they apply:
   - **Sequence diagram** for interactions over time: boot sequence, a sensor transaction, a CLI command, a mesh message round-trip. Include the hardware or the remote party as a participant.
   - **State machine diagram** for anything with modes or states: driver lifecycle, safe mode vs normal mode, provisioning, debounce logic.
   - **Class/structure diagram** for the data structures and interfaces the developer added: structs, vtables or ops tables, ownership, and the relationships between them.
   - **Component diagram** when the work spans several modules, to show the dependency direction.
5. **Things to notice:** a short list of the non-obvious points from Phase 2 (context, concurrency, timing, memory, failure modes) that apply to this code. Note any risks you see, phrased as questions to look into, not as fixes.

### Diagram format

- In the terminal, draw diagrams in **ASCII/Unicode** inside code blocks, so they render without any tools. Keep them under ~80 columns wide, and split a large diagram rather than cramming it.
- At the end, offer to save the whole explanation as `docs/learn/<branch-name>.md`, with the diagrams written in **Mermaid** (`sequenceDiagram`, `stateDiagram-v2`, `classDiagram`, `flowchart`) so they render on GitHub and in IDEs. Only write the file if the developer agrees.

## Phase 4 — Check understanding and answer questions

After each module, pause and ask the developer whether it is clear, and whether they want to go deeper into something (a function, a diagram, a hardware detail) or move on. Answer follow-up questions using the same tools: pseudocode, a smaller focused diagram, or a walk-through of one concrete scenario with real values (for example "a raw reading of 0x6666 goes through these steps...").

When everything in scope has been covered, finish with:
- a one-paragraph recap of the whole branch's work and how its pieces connect;
- a single top-level diagram (component or call graph) of all of the developer's changes together;
- the open risks or questions collected along the way.

## Rules

- Explain only the developer's own work on the branch. Surrounding code appears only as context, at the edge of the diagrams.
- Be accurate. Every pseudocode step and diagram arrow must match the actual code. If something is unclear from the code (for example which context a callback runs in), say so instead of guessing.
- Do not modify source files. The only file this skill may write is `docs/learn/<branch-name>.md`, and only with the developer's agreement.
- Do not invent hardware facts. Quote values from the code or the spec, and point to the datasheet when relevant.
- Pitch the explanation at a developer who wrote the code but may not yet fully understand the system around it: explain the embedded concepts that matter, and skip the generic C basics unless asked.
