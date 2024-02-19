#!/bin/sh

### Find script directory and load helper functions.
scriptdir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
. "${scriptdir}/shared_test_functions.sh"


### Project-specific constants and setup

out="${bindir}/tests-output"
out_valgrind="${bindir}/tests-valgrind"

msleep="${bindir}/tests/msleep/msleep"
lbs="${bindir}/lbs/lbs"
kvlds="${bindir}/kvlds/kvlds"

# Functions to help start and stop servers
. "${scriptdir}/kivaloo_servers.sh"

### Run tests using project-specific constants
run_scenarios
