#!/bin/sh
# Checks an installed kikarinhas: the versions match, every library the
# binaries need is there, the program runs the fake chat for a few seconds
# on a virtual X screen and quits cleanly on SIGTERM, and the configurator
# opens. Optionally runs the program under valgrind too.
#
# Usage: tools/smoke-test.sh [--valgrind]      (from the repository root)
# Needs xvfb-run and fonts; --valgrind needs valgrind. Touches nothing of
# the user's: HOME and the sockets live in a temporary folder.
set -eu

kit=docs/avatares/kit-exemplo
seconds=${SMOKE_SECONDS:-8}
use_valgrind=no
[ "${1:-}" = "--valgrind" ] && use_valgrind=yes

[ -d "$kit" ] || { echo "run it from the repository root ($kit not found)" >&2; exit 2; }
version=$(sed -n 's/^VERSION *= *//p' config.mk)

work=$(mktemp -d /tmp/kk-smoke.XXXXXX)
trap 'rm -rf "$work"' EXIT
export HOME=$work
unset XDG_DATA_HOME XDG_CONFIG_HOME XDG_RUNTIME_DIR
: > "$work/users.tsv"
: > "$work/kikarinhas.ini"

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

echo "== versions"
for b in kikarinhas kikarinhas-config; do
	got=$("$b" --version 2>&1) || fail "$b --version exited with an error: $got"
	[ "$got" = "$b $version" ] || fail "$b --version: \"$got\", expected \"$b $version\""
done

echo "== libraries"
for b in kikarinhas kikarinhas-config; do
	missing=$(ldd "$(command -v $b)" | grep "not found" || true)
	[ -z "$missing" ] || fail "$b: $missing"
done

echo "== installed files"
prefix=$(dirname "$(dirname "$(command -v kikarinhas)")")
for f in "$prefix/share/doc/kikarinhas/kikarinhas.ini" \
	"$prefix/share/applications/kikarinhas.desktop" \
	"$prefix/share/applications/kikarinhas-config.desktop"; do
	[ -f "$f" ] && continue
	# Minimal container images (Ubuntu's) tell dpkg to skip /usr/share/doc:
	# then it is enough that the package lists the file.
	if command -v dpkg > /dev/null 2>&1 && dpkg -L kikarinhas 2> /dev/null | grep -qx "$f"; then
		echo "   $f: in the package, skipped by dpkg's path-exclude"
		continue
	fi
	fail "missing $f"
done

# Runs the program on a virtual screen for $1 seconds, then sends SIGTERM. A
# clean quit has exit status 0 (--preserve-status gives the program's own).
demo() {
	secs=$1
	log=$work/demo.log
	status=0
	xvfb-run -a -s "-screen 0 1280x720x24" \
		timeout --preserve-status -s TERM "$secs" \
		kikarinhas --demo-chat --avatar-dir "$kit" --sa-dir off \
		-c "$work/kikarinhas.ini" --users "$work/users.tsv" \
		--socket "$work/kk.sock" -v > "$log" 2>&1 || status=$?
	if [ "$status" -ne 0 ]; then
		cat "$log" >&2
		fail "kikarinhas exited with status $status"
	fi
	grep -q "janela .* no modo obs" "$log" || { cat "$log" >&2; fail "no window was opened"; }
	grep -q "^\[demo\]" "$log" || { cat "$log" >&2; fail "the fake chat said nothing"; }
	if grep -q "erro:" "$log"; then
		cat "$log" >&2
		fail "errors in the log"
	fi
}

echo "== fake chat for ${seconds}s"
demo "$seconds"

echo "== configurator opens"
status=0
xvfb-run -a -s "-screen 0 1280x720x24" \
	timeout -s TERM 4 kikarinhas-config "$work/kikarinhas.ini" \
	> "$work/config.log" 2>&1 || status=$?
# 124: still running when the timeout came, which is what we want.
if [ "$status" -ne 124 ]; then
	cat "$work/config.log" >&2
	fail "kikarinhas-config exited with status $status"
fi

if [ "$use_valgrind" = yes ]; then
	echo "== valgrind (errors only; the binary is stripped, but invalid accesses still show)"
	status=0
	xvfb-run -a -s "-screen 0 1280x720x24" \
		timeout --preserve-status -s TERM 40 \
		valgrind --error-exitcode=99 --leak-check=no -q \
		kikarinhas --demo-chat --avatar-dir "$kit" --sa-dir off \
		-c "$work/kikarinhas.ini" --users "$work/users.tsv" \
		--socket "$work/kk.sock" > "$work/valgrind.log" 2>&1 || status=$?
	if [ "$status" -ne 0 ]; then
		cat "$work/valgrind.log" >&2
		fail "valgrind: status $status"
	fi
fi

echo "OK"
