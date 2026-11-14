#!/bin/sh
# Installs a .bar on the Q10 over root ssh, through the system installer's PPS job object, and
# prints what the installer answered. The route HyperBerry's notes describe
# (../hyperberry/docs/memory/q10-install-bar.md): the package in /var/rootdata, mode 644; a job
# object the installer may write its result into; three lines appended with echo (the phone has
# no printf). The user allowed writes over ssh for tests on 2026-11-14.
#
#   scripts/install-phone.sh build/bbmusic.bar
set -eu
PHONE=${PHONE:-bb10rootx}
BAR=$1
name=bbm$(date +%s)
scp -q -o BatchMode=yes "$BAR" "$PHONE:/var/rootdata/$name.bar"
ssh -o BatchMode=yes "$PHONE" "chmod 644 /var/rootdata/$name.bar
	J=/pps/system/installer/upd/current/job.$name.bar
	umask 000; touch \$J; chmod 666 \$J
	echo 'action::install' >> \$J
	echo 'package_location::/var/rootdata/$name.bar' >> \$J
	echo 'extras::' >> \$J
	i=0
	while [ \$i -lt 90 ]; do
		if grep '^result::' \$J >/dev/null 2>&1; then break; fi
		sleep 1; i=\$((i+1))
	done
	grep -E '^(result|errorstr|error|message)' \$J
	rm -f /var/rootdata/$name.bar"
