#!/bin/sh

### Find script directory and load helper functions.
scriptdir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
. ${scriptdir}/shared_test_functions.sh


### Project-specific constants and setup

if [ "${N:-0}" -gt "0" ]; then
	test_scenarios="$(printf "${scriptdir}/%02d-*.sh" "${N}")"
else
	test_scenarios="${scriptdir}/??-*.sh"
fi
out="${bindir}/tests-output"
out_valgrind="${bindir}/tests-valgrind"


### Run tests using project-specific constants
run_scenarios ${test_scenarios}
