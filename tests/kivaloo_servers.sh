#!/bin/sh

## servers_check_leftover():
# Repeated testing, especially when doing ctrl-c to break out of (suspected)
# hanging, can leave processes floating around, which is problematic for the
# next testing run.  Checking for this shouldn't be necessary for normal
# testing (as opposed to test-script development), but there's no harm in
# checking anyway.  Once identified, the user is notified (instead of
# automatically removing them) -- a failing test will require manual attention
# anyway.
#
# As a simplifying assumption for kivaloo, we assume that the system has no
# "real" kivaloo servers running.
servers_check_leftover() {
	# Check for lbs
	if has_pid "lbs/lbs" ; then
		printf "Error: Left-over lbs server from previous run\n" 1>&2
		exit 1
	fi
	if has_pid "kvlds/kvlds" ; then
		printf "Error: Left-over kvlds server from previous run\n" 1>&2
		exit 1
	fi
}

## lbs_start(addr, storage_directory, is_single, pidfile, read_delay,
#     description, blocksize=512):
# Start a new lbs server on address ${addr}, using ${storage_directory}, and
# storing the pid in ${pidfile}.  If ${is_single} is greater than 0, the
# server should exit after handling a single connection, and valgrind should
# not be used, even if ${USE_VALGRIND} would otherwise indicate that we
# should.  If ${read_delay} is greater than 0, add extra read latency of
# ${read_delay} nanoseconds.  Use ${description} for the test framework.
# If ${blocksize} is specified, set it accordingly; otherwise, default to 512.
lbs_start() {
	_lbs_start_addr=$1
	_lbs_start_stor=$2
	_lbs_start_is_single=$3
	_lbs_start_pidfile=$4
	_lbs_start_read_delay=$5
	_lbs_start_description=$6
	_lbs_start_blocksize=${7:-512}

	# Set up check-specific variables.
	setup_check "${_lbs_start_description}"
	_lbs_start_stderr="${s_basename}-${c_count_str}-lbs-server.stderr"

	# Make the storage directory.
	mkdir -p "${_lbs_start_stor}"

	# How to start the server.
	_lbs_start_cmd="${lbs} -s ${_lbs_start_addr}			\
	    -d ${_lbs_start_stor} -p ${_lbs_start_pidfile}		\
	    -b ${_lbs_start_blocksize}"

	# Add -1 to exit after a single connection (if applicable).
	if [ "${_lbs_start_is_single}" -gt "0" ]; then
		_lbs_start_cmd="${_lbs_start_cmd} -1"
	fi

	# Add extra read delay (if applicable).
	if [ "${_lbs_start_read_delay}" -gt "0" ]; then
		_lbs_start_cmd="${_lbs_start_cmd} -l ${_lbs_start_read_delay}"
	fi

	# Only use valgrind if it's a single-shot server.
	if [ "${_lbs_start_is_single}" -gt "0" ]; then
		_lbs_start_cmd="${c_valgrind_cmd} ${_lbs_start_cmd}"
	fi

	# Start the server.
	${_lbs_start_cmd} 2> "${_lbs_start_stderr}"
	echo "$?" > "${c_exitfile}"

	# Clean up zero-byte stderr, if applicable.
	if [ ! -s "${_lbs_start_stderr}" ]; then
		rm -f "${_lbs_start_stderr}"
	fi
}

## lbs_stop(pidfile, is_single):
# Wait for a lbs server to stop.  If it's not a single-shot server, kill it.
lbs_stop() {
	_lbs_stop_pidfile=$1
	_lbs_stop_is_single=$2

	# If we're not using a single-shot server and we have a pidfile,
	# then kill the server.
	if [ "${_lbs_stop_is_single}" -eq "0" ] &&		\
	    [ -e "${_lbs_stop_pidfile}" ]; then
		kill "$(cat "${_lbs_stop_pidfile}")"
	fi
	rm -f "${_lbs_stop_pidfile}"

	# Wait for lbs server to stop.
	wait_while 0 has_pid "lbs/lbs"

	# Give valgrind a chance to finish writing files.
	if [ -n "${c_valgrind_cmd}" ]; then
		wait_while 0 valgrind_incomplete
	fi
}

## kvlds_start(addr, lbs_addr, is_single, pidfile, extraargs,
#    expected_exitcode, description):
# Start a new kvlds server on ${addr}, using ${lbs_addr} as the lbs backend,
# storing the pid in ${pidfile}.  If ${is_single} is greater than 0, the
# server should exit after handling a single connection, and valgrind should
# not be used, even if ${USE_VALGRIND} would otherwise indicate that we
# should.  Pass ${extraargs} to the kvlds binary.  The kvlds server should
# exit with ${expected_exitcode}.  Use ${description} for the test framework.
kvlds_start() {
	_kvlds_start_addr=$1
	_kvlds_start_lbs_addr=$2
	_kvlds_start_is_single=$3
	_kvlds_start_pidfile=$4
	_kvlds_start_extraargs=$5
	_kvlds_start_expected_exitcode=$6
	_kvlds_start_description=$7

	# Set up check-specific variables; don't check for a previous exit file.
	setup_check "${_kvlds_start_description}" 0
	_kvlds_start_stderr="${s_basename}-${c_count_str}-kvlds.stderr"

	# How to start the server.
	_kvlds_start_cmd="${kvlds} -s ${_kvlds_start_addr}		\
	    -l ${_kvlds_start_lbs_addr} -p ${_kvlds_start_pidfile}	\
	    ${_kvlds_start_extraargs}"

	# Add -1 to exit after a single connection (if applicable).
	if [ "${_kvlds_start_is_single}" -gt "0" ]; then
		_kvlds_start_cmd="${_kvlds_start_cmd} -1"
	fi

	# Only use valgrind if it's a single-shot server.
	if [ "${_kvlds_start_is_single}" -gt "0" ]; then
		_kvlds_start_cmd="${c_valgrind_cmd} ${_kvlds_start_cmd}"
	fi

	# Start the server.
	${_kvlds_start_cmd} 2> "${_kvlds_start_stderr}"
	expected_exitcode "${_kvlds_start_expected_exitcode}" "$?"	\
	    > "${c_exitfile}"

	# Clean up zero-byte stderr, if applicable.
	if [ ! -s "${_kvlds_start_stderr}" ]; then
		rm -f "${_kvlds_start_stderr}"
	fi
}

## kvlds_stop(pidfile, is_single):
# Wait for a kvlds server to stop.  If it's not a single-shot server, kill it.
kvlds_stop() {
	_kvlds_stop_pidfile=$1
	_kvlds_stop_is_single=$2

	# If we're not using a single-shot server and we have a pidfile,
	# then kill the server.
	if [ "${_kvlds_stop_is_single}" -eq "0" ] &&			\
	    [ -e "${_kvlds_stop_pidfile}" ]; then
		kill "$(cat "${_kvlds_stop_pidfile}")"
	fi
	rm -f "${_kvlds_stop_pidfile}"

	# Wait for kvlds server to stop.
	wait_while 0 has_pid "kvlds/kvlds"

	# Give valgrind a chance to finish writing files.
	if [ -n "${c_valgrind_cmd}" ]; then
		wait_while 0 valgrind_incomplete
	fi
}
