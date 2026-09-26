---
name: release-notes
description: Write end-user release notes / changelog for a Buddy firmware version (alpha, RC or final) following Prusa's house style in doc/release_notes.md, from the git diff between the previous version tag and the RELEASE-X.Y branch. Use when asked for release notes, a changelog, "what changed in 6.x", or a CHANGELOG-X.Y.Z.md.
---

# Release notes

**The style guide and rules are in `doc/release_notes.md`. Read it fully first and follow it exactly.** It defines the title, the Summary section, the context paragraph, the H2 detail sections, the tone, the printer tags (`[XL]`, `[C1]`, `[C1/+]`, `[C1L]`, `[MK4 family]`, `[MMU]`), and what to omit (iX, translation updates, rewordings, dependency bumps, and fixes to features introduced in the same release). This skill adds only the mechanics.

## 1. Establish the range

The checkout may be shallow and without tags, so fetch them first:

```bash
git fetch --tags origin                       # add --unshallow if `git rev-parse --is-shallow-repository` says true
git fetch origin RELEASE-X.Y                  # the release branch; make sure it is up to date
git tag --list 'v*' --sort=-v:refname | head  # find the previous version tag
RANGE=vPREV..origin/RELEASE-X.Y
```

For an RC or final release after an earlier alpha or RC, the range usually starts at the last published tag of the same line. Confirm with the user if it's ambiguous. If `RELEASE-X.Y` or the tags don't exist in this remote (e.g. a fork without release branches), stop and ask which refs to compare.

## 2. Collect changes

```bash
git log --no-merges --format='%h %s%n%b%n---' $RANGE > /tmp/commits.txt
git log --no-merges --format='%h %s' $RANGE | grep -v -E '^[0-9a-f]+ (Update translations|git subrepo|Bump version)'
git diff --stat $RANGE -- src lib/Marlin include | tail -5
```

- **Grouping:** group commits by feature or ticket (the `BFW-xxxx` reference at the end of the commit body). **Warn the user about any commit without a BFW reference.**
- **Read the code, not just the message.** For each candidate item, inspect `git show <sha>`, since commit subjects can mislead. Decide what the *user* observes: symptom and resolution, never function names.
- **Drop:**
  - fixes to features that are new in this same release
  - refactors with no user-visible effect
  - translation updates
  - dependency bumps, except the bootloader or MMU firmware
  - iX-only changes
- **Bootloader bump:** if the bootloader version changed (`utils/bootstrap.py`, the `bootloader-*` entries), ask the user to explain the changes.
- **MMU bump:** if the MMU firmware changed (the `firmware-mmu` entry), mention that users must update it themselves.
- **Printer tags:** derive them from what the code touches, e.g. `#if PRINTER_IS_PRUSA_XL()`, `HAS_TOOLCHANGER`, a printer's config files, or presets.

## 3. Write

- **Output file:** `CHANGELOG-X.Y.Z.md` (e.g. `CHANGELOG-6.10.1.md`) in the repo root, unless the user asks otherwise.
- **Order:** most impactful items first.
- **Review links:** for review purposes, add `https://dev.prusa3d.com/browse/BFW-XXXX` links **only in the Summary bullets**. They are removed before publishing.
- **Screenshots:** point out items where a screenshot would help.
- **Pre-releases:** end the context paragraph with the sentence linking to the earlier alpha or RC notes.

## 4. Final checklist (from the style guide)

- Every H2 title matches its Summary bullet, including tags and `(#NNNN)` issue references.
- Descriptions match their titles and are written for end users.
- There are no emojis, no exclamation marks and no internal identifiers.
- Menu paths are in bold with arrows: **Settings → Hardware → …**.
- Notes are italic: `_NOTE: ..._`.
