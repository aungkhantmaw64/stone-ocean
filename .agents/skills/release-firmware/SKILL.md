---
name: release-firmware
description: Cut a firmware release. Collects the changes since the last release, proposes a semantic-version bump, updates firmware/version.txt and CHANGELOG.md on a release branch, and tags the release once the developer approves.
disable-model-invocation: true
argument-hint: "[optional: major | minor | patch | an exact version like 0.3.0]"
---

You are cutting **one firmware release**. The developer approves the bump and the changelog text before anything is committed, and approves again before anything is pushed or tagged.

Arguments: `$ARGUMENTS`. If they name a bump level or an exact version, use it in Step 3 in place of your own proposal, but still flag it if the changes suggest a different level.

## Where the version lives

- **`firmware/version.txt`** holds the version and nothing else: one line, `X.Y.Z`, with no `v` prefix. ESP-IDF reads this file into `PROJECT_VER`, which becomes `esp_app_desc_t.version`. The boot banner in `firmware/main/app_main.c` logs that value. Without the file, ESP-IDF uses `git describe`, so once the file exists it is the only source of truth for the version.
- **`CHANGELOG.md`** at the repo root follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/). Keep an `## [Unreleased]` section at the top. Each release section is `## [X.Y.Z] - YYYY-MM-DD`. Use these sub-headings in this order, and include only the ones that have entries: `Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`. Put compare links at the bottom of the file.
- **Git tag** `vX.Y.Z` marks the merge commit of the release PR on `main`.

If `version.txt` or `CHANGELOG.md` is missing, this is the **first release**. Create both. Propose `0.1.0` unless the developer says otherwise, and summarise the whole history in the changelog instead of listing every commit.

## Step 1 — Find the last release

Start from a clean, up-to-date `main` (`git status`, `git pull --ff-only`). If the tree is dirty, stop and tell the developer.

Find the last release with `git describe --tags --abbrev=0 --match 'v*'`. If there is no tag, use the root commit. Read the current `version.txt` and `CHANGELOG.md`.

Done when you know the previous version (or know this is the first release) and the commit range to review.

## Step 2 — Collect the changes

This repo merges every change through a PR, so **the merged PRs are the units of change**. List them with `git log --merges --oneline <range>`, and use `gh pr view <n>` for the title, body and linked issue. Pick up any direct commits with `git log --no-merges --first-parent <range>`. Include any entries already under `[Unreleased]`.

For each change, decide its changelog category and whether it is **breaking** (see below). Leave out changes a firmware user never sees, such as CI, tests only, refactors with no behaviour change, and tooling (`specs/`, `.agents/`, `.claude/`). List them as omitted so the developer can see them. Write each entry for someone flashing a node: say what the node does differently, not which file changed. End each entry with the PR reference, like `(#17)`.

Done when **every** merged PR in the range is either an entry or on the omitted list.

### What counts as breaking

A change is breaking if a node running the old firmware, an existing config, or an existing deployed mesh stops working with the new build without manual action:

- the config JSON format changes in a way that rejects old configs, or the `version` that `node_config_parse.c` accepts changes;
- `CONFIG_STORE_FORMAT_VERSION` changes, so the stored config is rejected or rewritten;
- `firmware/partitions.csv` changes the layout, so the node needs a full reflash instead of an app-only update;
- the protobuf wire format in `proto/` changes in a way that is not backward compatible (a field renumbered, a type changed, a required meaning changed);
- mesh models, opcodes or the composition data change, so the node must be re-provisioned;
- an NVS key or a format used by `node_health` changes without a migration.

Call out each breaking change in its entry, with the action the user has to take (for example "reflash the full image", "re-provision the node", "regenerate the config").

## Step 3 — Propose the bump

Use SemVer. While the major version is `0`, a breaking change bumps **minor** and everything else bumps **patch**. From `1.0.0` on: breaking means major, `Added` means minor, and fixes only mean patch. Only the developer decides to go to `1.0.0`.

Show the developer:
1. the old version, the new version, and the one change that sets the bump level;
2. the full draft of the new changelog section;
3. the omitted changes.

Ask them to approve or edit it. Done when the developer has approved the version and the text.

## Step 4 — Write and commit

1. Create the branch `release/vX.Y.Z` from `main`.
2. Write the new version to `firmware/version.txt`.
3. In `CHANGELOG.md`, move the approved text under `## [X.Y.Z] - <today>`, and leave `## [Unreleased]` empty above it. Update the compare links: `[Unreleased]` compares `vX.Y.Z...HEAD`, and `[X.Y.Z]` compares the previous tag with `vX.Y.Z`. For the first release, link to the tag.
4. If ESP-IDF is available (`idf.py --version`), run `scripts/build.sh` and check that `project_version` in `firmware/build/project_description.json` is the new version. If it is not available, say so in your report instead of claiming that the build was checked.
5. Commit both files with the message `Release vX.Y.Z`.

Done when the branch has one release commit containing exactly those two files.

## Step 5 — Publish and tag

Ask before each outward step:
1. Push the branch and open a PR titled `Release vX.Y.Z` (use the `pr` skill for the body if it is available), with the new changelog section as the summary.
2. After the developer confirms the PR is merged, check out `main`, pull, and run `git tag -a vX.Y.Z -m "vX.Y.Z"` on the merge commit, then `git push origin vX.Y.Z`.

Done when the tag `vX.Y.Z` on `origin` points at the merge commit on `main`, or when the developer chooses to stop before that. In that case, report exactly which steps are left.
