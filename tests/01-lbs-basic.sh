#!/bin/sh

### Constants
c_valgrind_min=1
testlbs="${scriptdir}/lbs/test_lbs"
stor="${s_basename}-stor"
sock="${stor}/sock"
lbs_pidfile="${sock}.pid"
testlbs_pidfile="${s_basename}-testlbs.pid"

### lbs-specific constants
lbs_basic_read_delay=1000000

## clean_storage():
# Remove any old lbs storage directory.
clean_storage() {
	rm -rf "${stor}"
}

## lbs_check_basic(is_single, is_read_delay, description):
# Start a lbs server and run the test_lbs binary on it.  If ${is_single} is 1,
# run the server in the "single connection" mode; otherwise, run test_lbs a
# second time.  If the server is not in "single connection" mode, do not use
# valgrind on the server, even if ${USE_VALGRIND} would otherwise indicate
# that we should use valgrind.  If ${is_read_delay} is non-zero, add a read
# delay to the server operations.  Use ${description} for the test framework.
lbs_check_basic() {
	is_single=$1
	is_read_delay=$2
	description=$3

	# Ensure that we're starting with a clean storage directory.
	servers_check_leftover
	clean_storage

	# Set up the read delay (if applicable).
	if [ "${is_read_delay}" -gt 0 ]; then
		this_read_delay="${lbs_basic_read_delay}"
	else
		this_read_delay="0"
	fi

	# Start a server.
	lbs_start "${sock}" "${stor}" "${is_single}"		\
		"${lbs_pidfile}" "${this_read_delay}"		\
		"lbs ${description}"

	# Run test.
	setup_check_variables "test_lbs ${description}"
	${c_valgrind_cmd} "${testlbs}" "${sock}"
	echo "$?" > "${c_exitfile}"

	# Run test again (if applicable).
	if [ "${is_single}" -eq "0" ]; then
		setup_check_variables "test_lbs ${description} again"
		${c_valgrind_cmd} "${testlbs}" "${sock}"
		echo "$?" > "${c_exitfile}"
	fi

	# Clean up.
	lbs_stop "${lbs_pidfile}" "${is_single}"
	clean_storage
}

## lbs_check_unclean_disconnect():
# Start running test_lbs, but disconnect it after 100ms.  Then run a new
# test_lbs and check that it completes correctly.
lbs_check_unclean_disconnect() {
	# Ensure that we're starting with a clean storage directory.
	servers_check_leftover
	clean_storage

	# Start a server.
	lbs_start "${sock}" "${stor}" "0"			\
		"${lbs_pidfile}" "${lbs_basic_read_delay}"	\
		"lbs server unclean disconnect"

	# Start a test, but disconnect it suddenly after 100ms.
	"${testlbs}" "${sock}" & echo $! > "${testlbs_pidfile}"
	"${msleep}" 100 && kill "$(cat "${testlbs_pidfile}")"
	rm "${testlbs_pidfile}"

	# Repeat the test, this time waiting for it to finish.
	setup_check_variables "test_lbs unclean disconnect"
	"${testlbs}" "${sock}"
	echo "$?" > "${c_exitfile}"

	# Clean up.
	lbs_stop "${lbs_pidfile}" "0"
	clean_storage
}

## lbs_check_connections_queue():
# Start test_lbs three times, 100ms apart.  Check that the last one completes
# successfully.
lbs_check_connections_queue() {
	# Ensure that we're starting with a clean storage directory.
	servers_check_leftover
	clean_storage

	# Start a server.
	lbs_start "${sock}" "${stor}" "0"			\
		"${lbs_pidfile}" "${lbs_basic_read_delay}"	\
		"lbs server unclean disconnect"

	# Start two tests almost at the same time.
	"${testlbs}" "${sock}" &
	"${msleep}" 100
	"${testlbs}" "${sock}" &
	"${msleep}" 100

	# Start the test one more time, this time waiting and checking the
	# exit code.
	setup_check_variables "test_lbs unclean disconnect"
	"${testlbs}" "${sock}"
	echo "$?" > "${c_exitfile}"

	# Clean up.
	lbs_stop "${lbs_pidfile}" "0"
	clean_storage
}

## lbs_check_connections_queue():
# Check lbs with ipv4 and ipv6 addresses.
lbs_check_addresses() {
	# Ensure that we're starting with a clean storage directory.
	servers_check_leftover
	clean_storage

	for S in "localhost:1234" "[127.0.0.1]:1235" "[::1]:1236"; do
		this_lbs_pidfile="${s_basename}-$S.pid"
		# Start a server.
		lbs_start "${S}" "${stor}" "0"				\
			"${this_lbs_pidfile}" "${lbs_basic_read_delay}"	\
			"lbs server ${S}"

		# Run the test.
		setup_check_variables "test_lbs check ${S}"
		"${testlbs}" "${S}"
		echo "$?" > "${c_exitfile}"

		# Clean up.
		lbs_stop "${this_lbs_pidfile}" "0"
		clean_storage
	done
}

### Actual command
scenario_cmd() {
	# Check lbs with the server in single-connection mode.
	lbs_check_basic 1 1 "single"

	# Check lbs with the server in single-connection mode without a read
	# delay.
	lbs_check_basic 1 0 "single"

	# Check lbs, with the server in normal mode, but still with a read
	# delay.  Do not use valgrind on the server. but still use it for the
	# test_lbs binary.
	lbs_check_basic 0 1 "normal"

	# Additional checks.  Do not use valgrind at all.
	lbs_check_unclean_disconnect
	lbs_check_connections_queue
	lbs_check_addresses
}
