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
}

# server_lbs_start(addr, storage_directory, is_single, pidfile, read_delay,
#     description):
# Start a new lbs server on address ${addr}, using ${storage_directory}, and
# storing the pid in ${pidfile}.  If ${is_single} is greater than 0, the
# server should exit after handling a single connection, and valgrind should
# not be used, even if ${USE_VALGRIND} would otherwise indicate that we
# should.  If ${read_delay} is greater than 0, add extra read latency of
# ${read_delay} nanoseconds.  Use ${description} for the test framework.
server_lbs_start() {
	addr=$1
	stor=$2
	is_single=$3
	pidfile=$4
	read_delay=$5
	description=$6

	# Set up check-specific variables.
	setup_check_variables "${description}"
	server_stderr="${s_basename}-${s_count}-lbs-server.stderr"

	# Make the storage directory.
	mkdir -p "${stor}"

	# How to start the server.
	server_cmd="${lbs} -s ${addr} -d ${stor} -p ${pidfile} -b 512"

	# Add -1 to exit after a single connection (if applicable).
	if [ "${is_single}" -gt "0" ]; then
		server_cmd="${server_cmd} -1"
	fi

	# Add extra read delay (if applicable).
	if [ "${read_delay}" -gt "0" ]; then
		server_cmd="${server_cmd} -l ${read_delay}"
	fi

	# Only use valgrind if it's a single-shot server.
	if [ "${is_single}" -gt "0" ]; then
		server_cmd="${c_valgrind_cmd} ${server_cmd}"
	fi

	# Start the server.
	${server_cmd} 2> "${server_stderr}"
	echo "$?" > "${c_exitfile}"

	# Clean up zero-byte stderr, if applicable.
	if [ ! -s "${server_stderr}" ]; then
		rm -f "${server_stderr}"
	fi
}

# server_lbs_stop(is_single):
# Wait for a lbs server to stop.  If it's not a single-shot server, kill it.
server_lbs_stop() {
	pidfile=$1
	is_single=$2

	# If we're not using a single-shot server and we have a pidfile,
	# then kill the server.
	if [ "${is_single}" -eq "0" ] && [ -e "${pidfile}" ]; then
		kill "$(cat "${pidfile}")"
	fi
	rm -f "${pidfile}"

	# Wait for lbs server to stop.
	while has_pid "lbs/lbs" ; do
		if [ "${VERBOSE}" -ne 0 ]; then
                        printf "Waiting to stop: lbs/lbs\n" 1>&2
                fi
                "${msleep}" 100
	done

	# Give valgrind a chance to finish writing files.
	if [ -n "${c_valgrind_cmd}" ]; then
		if [ "${VERBOSE}" -ne 0 ]; then
			printf "Giving extra time for valgrind to write"
			printf " the logfile\n" 1>&2
		fi
		"${msleep}" 300
	fi
}
