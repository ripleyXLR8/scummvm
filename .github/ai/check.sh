#!/usr/bin/env bash
# Checks for AI-proposed engines. See README-AI.md.
#
#   check.sh lint <base-ref>          PR rules: allowed paths, PROVENANCE.md, engine status
#   check.sh lint-all                 list every AI engine (used for upstream-sync PRs)
#   check.sh build <engine>...        configure with only these engines, build, check they register
#   check.sh activate <engine>        activation checks (after a sanitizer build)
#   check.sh playback <engine> <dir>  replay test/playthrough.r against game data in <dir>,
#                                     writes engines/<engine>/test/playback.log
#
# lint/lint-all write "engines=" and "activate=" to $GITHUB_OUTPUT when set.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)" || exit 1
AI=.github/ai
FAILED=0
fail() { echo "::error::$*"; FAILED=1; }

# Files the fork itself owns. Anything else outside engines/<name>/ is an upstream file.
FORK_FILES='^(CLAUDE\.md|AGENTS\.md|README-AI\.md|\.github/ai/.*|\.github/workflows/ai-.*\.yml|\.github/workflows/upstream-sync\.yml|\.github/PULL_REQUEST_TEMPLATE/ai-engine\.md)$'

# Status (build-by-default field) of an engine's configure.engine, from the worktree or a git ref.
status() {
	{ if [ -n "${2:-}" ]; then git show "$2:engines/$1/configure.engine" 2>/dev/null; else cat "engines/$1/configure.engine" 2>/dev/null; fi; } |
		grep -m1 '^add_engine' | xargs printf '%s\n' | sed -n 4p
}

# Every "## " section of template $1 has content in $2 (HTML comments don't count).
sections_filled() {
	local h
	while read -r h; do
		perl -0pe 's/<!--.*?-->//gs' "$2" | awk -v h="$h" '$0==h{s=1;next} s&&/^## /{exit} s&&NF{f=1} END{exit !f}' ||
			fail "$2: section '$h' is empty"
	done < <(grep '^## ' "$1")
}

# Hash of the engine code, excluding test/, so a playback log can be tied to the code it ran.
code_hash() {
	find "engines/$1" -path "engines/$1/test" -prune -o -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1
}

output() {
	echo "engines=$1"; echo "activate=$2"
	[ -n "${GITHUB_OUTPUT:-}" ] && printf 'engines=%s\nactivate=%s\n' "$1" "$2" >>"$GITHUB_OUTPUT"
}

lint() {
	local files f e engines activate=""
	files=$(git diff --name-only "$1"...HEAD) || { fail "cannot diff against $1"; return; }
	while read -r f; do
		[ -z "$f" ] && continue
		[[ $f =~ ^engines/[^/]+/ || $f =~ ^dists/msvc/[^/]+\.bat$ || $f =~ $FORK_FILES ]] ||
			fail "$f: this fork never modifies upstream files; PRs may only touch engines/<name>/ (see README-AI.md)"
	done <<<"$files"
	engines=$(sed -n 's#^engines/\([^/]*\)/.*#\1#p' <<<"$files" | sort -u)
	for e in $engines; do
		[[ $e =~ ^[a-z0-9_]+$ ]] || { fail "engines/$e: engine names must match [a-z0-9_]+"; continue; }
		[ -d "engines/$e" ] || continue # engine removed
		local p=engines/$e/PROVENANCE.md
		if [ ! -f "$p" ]; then
			fail "$p is missing: copy $AI/PROVENANCE.md (upstream engines can't be modified in this fork)"
			continue
		fi
		sections_filled "$AI/PROVENANCE.md" "$p"
		perl -0pe 's/<!--.*?-->//gs' "$p" | awk '/^## Kind$/{s=1;next} s&&NF{print;exit}' | grep -qxE 'reimplementation|original' ||
			fail "$p: '## Kind' must be 'reimplementation' or 'original'"
		case "$(status "$e")" in
		yes) activate+=" $e" ;;
		no) ;;
		*) fail "engines/$e/configure.engine: build-by-default must be 'no' (proposed) or 'yes' (active)" ;;
		esac
	done
	output "$(echo $engines)" "$(echo $activate)"
}

lint_all() {
	output "$(ls engines/*/PROVENANCE.md 2>/dev/null | cut -d/ -f2 | xargs)" ""
}

build() {
	local list e
	list=$(IFS=,; echo "$*")
	# shellcheck disable=SC2086 # CONFIGURE_FLAGS is a flag list
	./configure --disable-all-engines --enable-engine="$list" ${CONFIGURE_FLAGS:-} || { fail "configure failed"; return; }
	make -j"$(nproc)" 2>&1 | tee build.log || { fail "build failed"; return; }
	for e; do
		./scummvm --list-engines | grep -qE "^$e[[:space:]]" || fail "engine $e is not registered (scummvm --list-engines)"
	done
}

# Run the playthrough recording; output goes to $3.
run_playback() {
	local e=$1 data=$2 log=$3 save
	save=$(mktemp -d)
	cp "engines/$e/test/playthrough.r" "$save/"
	{
		echo "recording: $(sha256sum "engines/$e/test/playthrough.r" | cut -d' ' -f1)"
		echo "code: $(code_hash "$e")"
		SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 1800 ./scummvm --path="$data" --savepath="$save" \
			--record-mode=playback --record-file-name=playthrough.r --disable-display \
			--debugflags=eventrec -d1 --auto-detect 2>&1
		echo "exit: $?"
	} >"$log"
}

# Fail unless $2 is a clean, complete playback log for the current code of engine $1.
check_log() {
	local e=$1 log=$2
	grep -qx "recording: $(sha256sum "engines/$e/test/playthrough.r" | cut -d' ' -f1)" "$log" || fail "$log: was made with another recording"
	grep -qx "code: $(code_hash "$e")" "$log" || fail "$log: was made with other engine code, re-run check.sh playback"
	grep -q 'reason=EOF' "$log" || fail "$log: playback did not reach the end of the recording"
	grep -qx 'exit: 0' "$log" || fail "$log: scummvm did not exit cleanly"
	! grep -qE 'ERROR: AddressSanitizer|runtime error:|LeakSanitizer' "$log" || fail "$log: sanitizer errors"
}

activate() {
	local e=$1 d=engines/$1 url sha data
	# 1. No compiler warnings in the engine's own code.
	! grep -E "^$d/[^:]+:[0-9]+(:[0-9]+)?: warning:" build.log || fail "$d: fix the compiler warnings above"
	# 2. Human review checklist.
	if [ -f "$d/REVIEW.md" ]; then
		sections_filled "$AI/REVIEW.md" "$d/REVIEW.md"
		! grep -n -- '- \[ \]' "$d/REVIEW.md" || fail "$d/REVIEW.md: unchecked items above"
	else
		fail "$d/REVIEW.md is missing: copy $AI/REVIEW.md"
	fi
	# 3. Detection + playthrough under ASan/UBSan.
	[ -f "$d/test/playthrough.r" ] || { fail "$d/test/playthrough.r is missing (record one with --record-mode=record)"; return; }
	if [ -f "$d/test/data.url" ]; then
		read -r url sha <"$d/test/data.url"
		data=$(mktemp -d)
		curl -fsSL "$url" -o "$data/dl" && echo "$sha  $data/dl" | sha256sum -c - || { fail "$d/test/data.url: download or sha256 failed"; return; }
		(cd "$data" && { unzip -q dl 2>/dev/null || tar xf dl 2>/dev/null || true; })
		./scummvm --detect --recursive --path="$data" | tee detect.log | grep -q "^$e:" || fail "$e: game not detected in test data"
		run_playback "$e" "$data" playback.log
		check_log "$e" playback.log
	elif [ -f "$d/test/playback.log" ]; then
		echo "No public test data: checking the maintainer-provided $d/test/playback.log"
		check_log "$e" "$d/test/playback.log"
	else
		fail "$d/test: add data.url (free data) or playback.log (from check.sh playback)"
	fi
}

case "${1:-}" in
lint) lint "$2" ;;
lint-all) lint_all ;;
build) shift; build "$@" ;;
activate) activate "$2" ;;
playback) run_playback "$2" "$3" "engines/$2/test/playback.log"; check_log "$2" "engines/$2/test/playback.log" ;;
*) sed -n '2,11p' "$0"; exit 2 ;;
esac
exit $FAILED
