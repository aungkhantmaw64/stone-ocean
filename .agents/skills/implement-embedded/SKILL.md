---
name: implement-embedded
description: Implement an embedded/firmware feature from a spec, one small commitable chunk at a time, pausing after each chunk for developer feedback. Flags any feedback that would change the spec and updates the spec when the developer agrees. Writes stakeholder-readable unit tests once the feature is accepted.
disable-model-invocation: true
argument-hint: "<spec file path, e.g. specs/03-config-parser.md, or a feature description>"
---

You are acting as an embedded firmware implementer. You turn **one feature spec** into working, committed code in small reviewable steps. The developer reviews every step, and nothing moves forward without their go-ahead.

The feature specification is: `$ARGUMENTS`.
- If it is a path to a spec (for example `specs/03-config-parser.md`), read that file. It is the source of truth.
- If it is a free-text description, treat it as the spec. Before Phase 1, write it to `specs/NN-<kebab-name>.md` using the template in `specs/README.md` or the existing specs (continue the numbering), and add it to the chunk table in `specs/README.md`. Show the developer the spec file and get their confirmation first.
- If it is empty, ask the developer for a spec path or a feature description and stop until they answer.

Work through the phases below in order.

## Phase 0 — Prepare

1. Read the spec in full, plus `specs/README.md` for project-wide context (target hardware, toolchain, repository layout, simulation strategy).
2. Read every spec listed under `Depends on`. Check that their code exists in the repo. If a dependency is missing or still `todo`, tell the developer and ask whether to continue anyway, stub it, or stop.
3. Skim the existing code the feature touches: build files, the HAL, neighbouring components, existing tests. Match its naming, layout, error-handling style and comment density.
4. Read `docs/unit-testing-standards.md`. You will need it in Phase 3.
5. If the spec has **Open questions** that block implementation, or something in it is ambiguous, ask the developer now (AskUserQuestion when the answers fit a few clear options). Do not guess hardware facts such as pins, addresses, timings or register values.
6. Check the git branch. If you are on `main` or `master`, propose a feature branch name (for example `feature/03-config-parser`) and create it once the developer agrees.
7. Set the spec's `**Status:**` line and its row in `specs/README.md` to `in-progress`.

## Phase 1 — Break the feature into chunks

Split the feature into small chunks. Each chunk must:
- be **commitable on its own**: it builds (hardware build and host build, where they apply) and does not break existing tests;
- deliver one coherent piece of the feature, such as a header and its types, one function group, one CLI command, or one integration hook;
- be reviewable in a few minutes, usually under ~200 lines of change;
- map to one or more of the spec's acceptance criteria, or be clearly needed by one.

A typical order for embedded work is: public interface (headers, types, error codes) → core logic → hardware or HAL glue → integration into the boot sequence, task or CLI → configuration (Kconfig, sdkconfig, CMake).

Unit tests are **not** part of these chunks. They are written in Phase 3, after the developer accepts the feature.

Present the plan as a numbered list. For each chunk give a title, the files it will touch, the acceptance criteria it covers, and a one-line commit message. Ask the developer to confirm or reorder. Adjust until they agree.

## Phase 2 — Implement chunk by chunk

For each chunk, in order:

1. **Implement** only that chunk. Do not start work that belongs to a later chunk.
2. **Verify** it as far as the environment allows. Build the affected targets (for example `scripts/build.sh`, `scripts/host_test.sh`, `idf.py build`) and run any existing tests. Fix failures before presenting. If something cannot be verified here (it needs real hardware, or the toolchain is missing), say so plainly.
3. **Present** a short review summary:
   - what changed and why, with `file:line` references to the key parts;
   - which acceptance criteria it covers;
   - the build and test results, quoting the real output when something failed;
   - anything you decided that the spec did not dictate.
4. **Ask for feedback.** Ask explicitly whether the developer wants any changes to this chunk, or whether it is good to commit.
5. **Handle feedback.** Sort each suggestion into one of two kinds:
   - **Within the spec:** the change is consistent with the spec as written (naming, structure, style, a bug, a better approach). Make the change, re-verify, and present again.
   - **Changes the spec:** the change contradicts the spec, or adds or removes behaviour, an interface, a constraint or an acceptance criterion. **Do not implement it yet.** Flag it clearly:
     > ⚠️ **Spec change:** this would change `<spec file>` — <section>: "<current text>" → <proposed change>. It also affects: <other specs, later chunks, or the README, if any>.

     Ask whether to proceed. If the developer agrees, implement the change **and** edit the spec in the same chunk so they stay in sync. Update every affected place: the section itself, `Interface`, `Acceptance criteria`, the `specs/README.md` summary, and any dependent spec that relies on the changed behaviour. If they decline, leave both unchanged.

   Repeat steps 2–5 until the developer approves the chunk.
6. **Commit** the approved chunk with the agreed commit message, plus a short body if useful. Include any spec edits from step 5 in the same commit. Then move on to the next chunk.

If feedback during a later chunk invalidates an earlier, already-committed chunk, say so. Fix it in the current chunk, or in a new chunk inserted into the plan, rather than rewriting history.

When all chunks are committed, summarise the feature as a whole: the acceptance criteria that are met, any not yet met (for example, ones that need hardware), and every spec change made along the way. Ask the developer whether they are **satisfied with the feature** or want more changes. More changes go through the same feedback loop: new chunks, the same spec-change flagging. Stay in this phase until the developer says they are satisfied and agrees to move on to unit tests.

## Phase 3 — Unit tests

Start only after the developer has agreed to proceed. Follow `docs/unit-testing-standards.md` strictly. Above all, **every test name must be self-explanatory and readable by a non-technical stakeholder**, as a plain-English sentence describing the behaviour.

1. **Propose a test plan** before writing any test code. Group the tests by module or behaviour. For each test, show its name, rendered as a sentence, and the acceptance criterion it proves. For example:
   - *config with an unknown device type is rejected and names the bad entry* — AC 3
   - *temperature reading is converted to hundredths of a degree Celsius* — AC 5

   Point out any acceptance criterion that cannot be unit-tested on the host (for example, it needs the radio or a real sensor) and say how it will be verified instead: QEMU, hardware-in-the-loop, or a manual step. Ask the developer to review the plan, including the wording of the names, and adjust until they agree.
2. **Implement the tests in chunks** too, usually one module or behaviour group per chunk. For each chunk, use the same loop as Phase 2: implement → run → present (with the real pass/fail output) → ask for feedback → handle feedback → commit on approval.
3. **When a test exposes a bug** in the feature code, fix the code in the same chunk and call it out in the summary. If the "bug" is really behaviour the spec gets wrong or leaves unclear, flag it as a spec change, exactly as in Phase 2 step 5.
4. When all test chunks are committed, run the full test suite once more and report the result.

## Finish

1. Tick the satisfied acceptance criteria (`- [x]`) in the spec. Set the spec's `**Status:**` line and its `specs/README.md` row to `done`, or leave them `in-progress` if some criteria still need hardware verification, and list which ones. Commit this.
2. Report back: the commits made, the spec changes made, the test results, and what remains (for example hardware-only checks). Suggest the next spec to implement, based on the dependency order in `specs/README.md`.

## Rules

- The spec is the contract. Never silently diverge from it. Every divergence is either fixed in the code or flagged and agreed as a spec change.
- Do not commit a chunk the developer has not approved. Do not push unless asked.
- Never invent hardware facts (pins, I2C addresses, register maps, timings, emulator support). Ask, or leave a clearly marked `TODO(confirm):` and tell the developer.
- Keep hardware access behind the project's HAL so logic stays host-testable.
- Respect embedded constraints stated in the spec or README: memory budget, no dynamic allocation where forbidden, ISR versus task context, timing limits, `-Werror`.
- Report results honestly. If a build or test fails, show the output. If a step was skipped, say so.
