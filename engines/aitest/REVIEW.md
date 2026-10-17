# Activation review: aitest

<!-- Copy this file to engines/<name>/REVIEW.md in the PR that sets the engine's status to "yes".
     CI fails if a section is empty or a box is left unchecked. -->

## Reviewers
<!-- GitHub handles of the reviewers. They must also approve the PR. -->
@ripleyXLR8

## Playtest
<!-- What was played, with which data version, and how far (to the end, or to which point).
     List known bugs. -->
Pipeline test: start, then quit after 5 seconds.

## Checklist
- [x] I read all of the engine code.
- [x] I checked the claims in PROVENANCE.md (clean room, no copyrighted code or assets committed).
- [x] The engine only uses ScummVM APIs for files, saves, graphics, sound, input and timing: no direct OS, network or filesystem calls.
- [x] Saving and loading work, including loading from the launcher.
- [x] The recording in test/playthrough.r covers the playtest described above.
