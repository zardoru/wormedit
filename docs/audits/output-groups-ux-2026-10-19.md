# Output groups: UI and UX suggestions (2026-10-19)

Feedback: the Project tab's output groups are "confusing". This note lists what a user sees,
why it confuses, and proposed changes in priority order. The proposal mockup is
[output-groups.svg](../chart-editor-plan/mockups/output-groups.svg). It is a set of suggestions,
not approved cards; the owner picks which become cards.

Read against `src/ui/nuklear/setup_panel.cpp` (`render_output_groups`, `render_output_group_row`,
`render_output_group_editor`, StepMania policy form) and `src/ui/nuklear/compose_resources.cpp`
(the **Export output group** dialog), at `412c0b4`. Earlier walkthrough findings are in
[user-walkthrough-osu-stepmania-2026-10-18.md](user-walkthrough-osu-stepmania-2026-10-18.md)
§2, §3, §9; T-712–T-714 fixed their behavior, not the presentation.

## What confuses today

1. **Two Export buttons, two policies.** The section header has **Export…** (a dialog that
   picks a group again and has a "Replace existing chart files" checkbox). Every row has
   **Export**, which never replaces files. The same word does different things in two places,
   and the row button gives no way to replace files.
2. **The StepMania settings live in a separate section.** "StepMania export policy" sits below
   the groups as a project-wide form. Its **File family** (SM/SSC) can disagree with a group
   whose path ends in `.ssc`. New 3K–8K and 10K projects start with separate *StepMania SM*
   and *StepMania SSC* groups, so the same choice exists in two places. Its closing text
   ("stage a typed conversion request … separate transactional steps") uses internal terms.
3. **"Output group" names the mechanism, not the goal.** Users want "export my charts as BMS
   to this folder". Neither "group" nor the **Format** column (a "family") says that.
4. **Format and path are set separately but must agree.** The editor has a format combo and a
   free-text "Output path" with the hint "Project-relative file or directory; use a directory for
   multiple BMS/BMSON/osu! charts". A new group defaults to `exports/chart.bmson` whatever format
   is chosen. The user must know which formats take one file per chart and which take one file
   for the whole song.
5. **The editor opens far from the row.** **Edit** and **New group** open the form under the
   whole table, not by the row. Its buttons are **Cancel group edit** and **Save group**.
   Deletion needs a **Confirm group deletion** checkbox before **Delete group**, unlike the
   confirmation dialogs elsewhere.
6. **Membership is edited in one place and shown in another.** The chart table's **Groups**
   column shows membership, but changing it means Edit → *Included charts* → Save. The checklist
   doesn't show charts the format can't hold (for example a 14K chart in a StepMania group).
7. **Preview output is unlabeled text.** Each row's preview status prints as wrapped lines under
   the row without a heading. It reports counts, not the files, conflicts or warnings
   (walkthrough §9).
8. **Seven columns, three of them unlabeled.** The row is Name · Format · Output path · Charts
   · (Edit) · (Preview) · (Export). The path column truncates the most useful information.

## Suggestions

### High value, mostly presentation

- **S1: One Export action per target, with the overwrite policy built in.** Replace the row
  **Export** and the header **Export…** with a split button on each target: **Export** uses
  the target's saved policy, and **▾** offers *Replace existing* / *Keep existing* for that
  run. Add **Export all** to the header. This removes the second dialog. Compose's **File →
  Export** can open the same split choice for the active chart's targets.
- **S2: Move format options into the target.** Show SM/SSC, timing syntax (*Warps as*) and
  implementation (*Check for*) inside the editor when the format is StepMania, and remove the
  project-wide form. *SM* and *SSC* then become one choice, and the default projects can start
  with one StepMania target. If owners want a project-wide default, keep it in Options and let
  targets inherit it. Data note: this makes `stepmania_export_policy_t` per output group and
  needs a native-format field.
- **S3: Rename to "Exports" / "export target".** Change the section heading, buttons and
  manual text. Keep `output_group` in code; the rename is user-facing text only.
- **S4: Choose a folder; WormEdit derives the file names.** Replace the free-text path with
  **Save to** plus **Browse…**. One-file-per-chart formats (BMS, BMSON, osu!) take a folder.
  StepMania takes a song file, and the dialog says "One .ssc holds every chart". Show the
  resolved file names live (S6). New targets default to `exports/<format>/`, not
  `exports/chart.bmson`.
- **S5: Show each target as a card.** A card has a format badge, the name, `→ destination`,
  chart chips with **+** to add (click a chip to remove it), and a one-line status: *Not
  exported yet* / *Exported 10:42 · 3 charts · 12 sounds copied* / *1 conversion warning —
  view*. Edit, Preview, Duplicate and Delete… go in the **⋯** menu. This replaces the
  seven-column row and the unlabeled preview lines.

### Medium value

- **S6: Preview lists files.** Expanding a card lists each planned file with *new* / *replace*
  / *copy* and each conversion warning in words. This is the "smallest fix" from walkthrough §9
  as a UI. It needs the export plan from T-714 to expose paths and per-file state, not counts.
- **S7: Edit in a dialog.** Use the shared dialog rules ([unsaved-changes.svg](../chart-editor-plan/mockups/unsaved-changes.svg)):
  title *Edit export · <name>*, **Delete…** on the left opening the standard confirmation,
  **Cancel**/**Save** on the right, and Esc to cancel. Drop the deletion checkbox.
- **S8: Membership from both sides, with compatibility.** In the editor, charts the format
  can't hold are disabled with the reason ("no 14-lane step type"). In the chart table, clicking
  the **Groups** cell opens a checklist of targets. Both use the existing `apply_set_output_group_command` path.
- **S9: An "Existing files" policy per target.** *Ask* / *Replace* / *Keep*, set in the editor
  and used by the split button's default. *Ask* shows a conflict list (from S6) before writing.

### Lower value or needs a decision

- **S10: Empty state.** "No exports yet" plus one button per format the project's layout
  suggests (from `new_project_layout_descriptor_t::default_output_groups`), not a blank form.
- **S11: Status survives restarts.** Showing "Exported 10:42" after reopening needs the last
  export time and result stored in the project or in session recovery state. Decide whether
  that belongs in the document; otherwise keep it session-only.
- **S12: Manual.** `walkthrough-bms.adoc` line 104 ("Under *Groups* … click *Edit*") and
  `walkthrough-osu-stepmania.adoc` describe the current flow; update them with whatever lands.

## Suggested order

S3 + S5 + S7 first (presentation only, no data changes), then S1 + S9 (one export path), then
S2 (needs a per-group policy in the document and native persistence), then S4 + S6 (need the
export plan to resolve names before writing). S8, S10–S12 can follow independently.
