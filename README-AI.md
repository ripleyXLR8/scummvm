# ScummVM AI engines fork

This is a fork of [ScummVM](https://github.com/scummvm/scummvm) where anyone can propose a game engine written with AI help. We accept two kinds of engine:

- **Reimplementations** of existing games, the usual ScummVM model. Users supply the original data files.
- **Original games** that use ScummVM as a cross-platform runtime.

For everything else (building, platforms, the engine API), the upstream [README](README.md) and the [ScummVM wiki](https://wiki.scummvm.org/) apply.

## Rules

1. **Never modify upstream files.** A PR may only add or change files under `engines/<name>/`. The one exception is `dists/msvc/<name>.bat`, which `create_engine` generates. Keeping changes additive is what lets the fork merge upstream every day without conflicts. The fork's own files are listed in `FORK_FILES` in [`.github/ai/check.sh`](.github/ai/check.sh). If an engine needs a change in common code, propose that change upstream.
2. **Every engine has a `PROVENANCE.md`**, copied from [the template](.github/ai/PROVENANCE.md) with every section filled in.
3. **Code is GPLv3+. Reimplementations are clean-room:** no decompiled, leaked or copyrighted code pasted in verbatim, and no copyrighted game assets committed.
4. A human takes responsibility for every line, AI-written or not.
5. **Disclose AI help with an `Assisted-by: AGENT:MODEL` commit trailer, not `Co-Authored-By`.** The upstream workflow `check-commit-authors.yml` (inherited, so this fork can't change it) closes any PR whose commits list an AI as co-author.

## Proposing an engine (status `no`)

```sh
make devtools/create_engine/create_engine   # or build it with your compiler
devtools/create_engine/create_engine mygame # generates engines/mygame/ with status "no"
cp .github/ai/PROVENANCE.md engines/mygame/
./configure --disable-all-engines --enable-engine=mygame && make -j
```

Open a PR using the AI engine template: add `?template=ai-engine.md` to the PR URL. The **AI engines** workflow checks the following:

- The PR touches only allowed paths.
- `PROVENANCE.md` is complete.
- The engine's status in `configure.engine` is `no` or `yes`.
- The engine builds on its own and registers with ScummVM (`scummvm --list-engines`).

Once merged with status `no`, the engine is **proposed**. Default builds leave it out, so users don't get it. Testers can build it with `--enable-engine=<name>`.

## Activating an engine (status `yes`)

Open a PR that sets `yes` in `engines/<name>/configure.engine`. In the same PR, add:

- `engines/<name>/REVIEW.md`, from [the template](.github/ai/REVIEW.md), with all sections filled and all boxes checked.
- `engines/<name>/test/playthrough.r`: a recording made with ScummVM's event recorder. Configure with `--enable-eventrecorder`, then run `scummvm --record-mode=record --record-file-name=playthrough.r <target>`. The file is written to your save directory.
- Test data, in one of two forms:
  - **Freely licensed data** (original games): add `engines/<name>/test/data.url`, one line containing `<url> <sha256>` of a zip or tarball.
  - **Copyrighted data** (reimplementations): a maintainer who owns the game builds with `--enable-asan --enable-eventrecorder` and runs `.github/ai/check.sh playback <name> <game dir>`. This writes `engines/<name>/test/playback.log`, which they commit. The log records hashes of the recording and of the engine code. If the code changes, the log no longer matches and has to be regenerated.

The **activation** job runs on any PR that touches an engine whose status is `yes`. It builds with AddressSanitizer and UndefinedBehaviorSanitizer and checks the following:

- No compiler warnings in the engine's code.
- `REVIEW.md` is complete.
- The game is detected in the test data.
- The recording plays to the end with no crash and no sanitizer errors.

The **AI engine approvals** workflow also requires approvals on the PR's latest commit from people with write access. The default is 2; change it with the repository variable `AI_REQUIRED_APPROVALS`.

## Staying in sync with upstream

The **Upstream sync** workflow runs every day and can also be started by hand from the Actions tab. It merges `scummvm/scummvm` `master` into the `upstream-sync` branch and opens a PR to `master`. That PR rebuilds every AI engine against the new upstream. If one breaks, upstream has changed an API that engine uses: ping the engine's responsible human. Always merge that PR with **Create a merge commit**, never squash or rebase.

## Maintainer setup

- Add the secret `SYNC_TOKEN`: a fine-grained PAT on this repo with Contents, Pull requests and Workflows set to read/write.
- Protect `master` and make these checks required:
  - `lint`
  - `build`
  - `activation`
  - `approvals`

  A job that has nothing to check is skipped, and GitHub counts a skipped job as passing.
