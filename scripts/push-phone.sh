#!/bin/sh
# Copies a BB10 binary to the Q10 over ssh into a scratch folder of our own, makes it runnable,
# and prints its path there. Extra files (a root bundle, a test track) are copied beside it.
#
#   scripts/push-phone.sh build/gate03 [file ...]
#   PROBE_ARG=-h scripts/push-phone.sh build/bbmusicd-gate     (a program that serves when run bare)
#
# The program is run once to see that the phone will execute it: bare, or with $PROBE_ARG. It
# must return 0 at once when started that way.
# After HyperBerry's push-engine.sh: BB10 runs a copied binary only once
# `/proc/boot/pathtrust !<file>` trusts that one file, and a file it refused stays refused, even
# as a new file that reuses its inode. So each build gets a fresh name, and a refused copy is
# emptied and parked in denied/ before the next try. Older copies of the same program go.
# Nothing outside $DIR and /dev/shmem is written on the phone.
set -eu
PHONE=${PHONE:-bb10rootx}
DIR=/root/bbmusic
SRC=$1
shift
base=$(basename "$SRC")
name=$base-$(shasum -a 256 "$SRC" | cut -c1-8)
ssh -o BatchMode=yes "$PHONE" "mkdir -p $DIR/denied"
scp -q -o BatchMode=yes "$SRC" "$PHONE:/dev/shmem/$name"
for extra in "$@"; do scp -q -o BatchMode=yes "$extra" "$PHONE:$DIR/"; done
ssh -o BatchMode=yes "$PHONE" "cd $DIR && for n in 1 2 3 4 5 6; do
	f=$name-\$n
	[ -e \$f ] || { cp /dev/shmem/$name \$f && chmod +x \$f && /proc/boot/pathtrust !$DIR/\$f; }
	if ./\$f ${PROBE_ARG:-} >/dev/null 2>&1; then
		for o in $base-*; do [ \$o = \$f ] || rm -f \$o; done
		rm -f /dev/shmem/$name; echo $DIR/\$f; exit 0
	fi
	: > \$f; mv \$f denied/
done
rm -f /dev/shmem/$name; echo 'no copy would run' >&2; exit 1"
