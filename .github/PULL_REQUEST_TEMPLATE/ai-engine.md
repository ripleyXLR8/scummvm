<!-- AI engine PR. Rules and process: README-AI.md -->

Engine: `engines/<name>/`
Kind: reimplementation / original
Status after this PR: `no` (proposed) / `yes` (activation)

- [ ] Only files under `engines/<name>/` are touched (plus `dists/msvc/<name>.bat`).
- [ ] `engines/<name>/PROVENANCE.md` is filled in and accurate.
- [ ] I have read all of the code in this PR and take responsibility for it.
- [ ] It builds with `./configure --disable-all-engines --enable-engine=<name> && make` without warnings.
- [ ] Activation only: `REVIEW.md`, `test/playthrough.r` and `test/data.url` or `test/playback.log` are included.
