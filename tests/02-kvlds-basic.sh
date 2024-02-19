#!/bin/sh

### Constants
c_valgrind_min=1
test_kvlds="${scriptdir}/kvlds/test_kvlds"
stor="${s_basename}-stor"
lbs_sock="${stor}/lbs_sock"
lbs_pidfile="${lbs_sock}.pid"
kvlds_sock="${stor}/kvlds_sock"
kvlds_pidfile="${kvlds_sock}.pid"
testkvlds_pidfile="${s_basename}-testkvlds.pid"

### lbs-specific constants
lbs_basic_read_delay=1000000

## clean_storage():
# Remove any old lbs storage directory.
clean_storage() {
	rm -rf "${stor}"
}

_kvlds_check_basic() {
	_kvlds_check_basic_single=$1
	_kvlds_check_basic_description=$2

	# Ensure that we're starting with a clean storage directory.
	servers_check_leftover
	clean_storage

	# Start the servers.
	lbs_start "${lbs_sock}" "${stor}" "0"				\
		"${lbs_pidfile}" "${lbs_basic_read_delay}"		\
		"lbs ${_kvlds_check_basic_description}"
	kvlds_start "${kvlds_sock}" "${lbs_sock}"			\
		"${_kvlds_check_basic_single}"				\
		"${kvlds_pidfile}" "-v 104 -C 1024" "0"			\
		"kvlds ${_kvlds_check_basic_description}"

	# Run test.
	setup_check "test_kvlds ${_kvlds_check_basic_description}"
	${c_valgrind_cmd} "${test_kvlds}" "${kvlds_sock}"
	echo "$?" > "${c_exitfile}"

	# Clean up
	kvlds_stop "${kvlds_pidfile}" "${_kvlds_check_basic_single}"
	lbs_stop "${lbs_pidfile}" "0"
	clean_storage
}

## _lbs_old_blocks():
# Return 0 if there are more than 1 blocks in the LBS storage directory.
_lbs_old_blocks() {
	_lbs_old_blocks_num=$(find "${stor}" -name "blks_*" | wc -l)

	# Do we have more than 1 block?
	test "${_lbs_old_blocks_num}" -gt 1
}

_kvlds_check_repeated() {
	_kvlds_check_repeated_description=$1

	# Ensure that we're starting with a clean storage directory.
	servers_check_leftover
	clean_storage

	# Start the servers.
	lbs_start "${lbs_sock}" "${stor}" "0" "${lbs_pidfile}"		\
		"${lbs_basic_read_delay}"				\
		"lbs ${_kvlds_check_repeated_description}"
	kvlds_start "${kvlds_sock}" "${lbs_sock}" "0"			\
		"${kvlds_pidfile}" "-v 104 -C 1024" "0"			\
		"kvlds ${_kvlds_check_repeated_description}"

	# Run test.
	setup_check "test_kvlds ${_kvlds_check_repeated_description}"
	${c_valgrind_cmd} "${test_kvlds}" "${kvlds_sock}"
	echo "$?" > "${c_exitfile}"

	# Check that lbs deleted all old blocks; wait up to 10 seconds.
	setup_check "verify that old blocks got deleted"
	wait_while 10000 _lbs_old_blocks
	echo "$?" > "${c_exitfile}"

	# Run tests twice
	setup_check "test_kvlds ${_kvlds_check_repeated_description} twice 1"
	${c_valgrind_cmd} "${test_kvlds}" "${kvlds_sock}"
	echo "$?" > "${c_exitfile}"
	setup_check "test_kvlds ${_kvlds_check_repeated_description} twice 2"
	${c_valgrind_cmd} "${test_kvlds}" "${kvlds_sock}"
	echo "$?" > "${c_exitfile}"

	# Stop the kvlds server.
	kvlds_stop "${kvlds_pidfile}" "0"

	# Clean up
	lbs_stop "${lbs_pidfile}" "0"
	clean_storage
}

_kvlds_check_large() {
	_kvlds_check_large_description=$1

	# Ensure that we're starting with a clean storage directory.
	servers_check_leftover
	clean_storage

	# Start lbs (with 512-byte blocks).
	lbs_start "${lbs_sock}" "${stor}" "0"			\
		"${lbs_pidfile}" "${lbs_basic_read_delay}"	\
		"lbs ${_kvlds_check_large_description}"

	# Try to start kvlds, but 96-byte keys are too large for lbs' blocks.
	kvlds_start "${kvlds_sock}" "${lbs_sock}" "0"		\
		"${kvlds_pidfile}" "-k 94 -v 32" "1"		\
		"kvlds -k 96 ${_kvlds_check_large_description}"
	rm "${kvlds_sock}"

	# Try to start kvlds, but 200-byte values are too large for lbs' blocks.
	kvlds_start "${kvlds_sock}" "${lbs_sock}" "0"		\
		"${kvlds_pidfile}" "-v 200" "1"			\
		"kvlds -v 200 ${_kvlds_check_large_description}"
	rm "${kvlds_sock}"

	# Start kvlds with 50-byte values; the server should be ok...
	kvlds_start "${kvlds_sock}" "${lbs_sock}" "0"		\
		"${kvlds_pidfile}" "-v 50" "0"			\
		"kvlds -v 50 ${_kvlds_check_large_description}"
	# ... but the test for 50-byte values should fail.
	setup_check						\
		"test_kvlds ${_kvlds_check_large_description} 50-byte values"
	${c_valgrind_cmd} "${test_kvlds}" "${kvlds_sock}"	\
	    2> "${s_basename}-v50.stderr"
	expected_exitcode 1 "$?" > "${c_exitfile}"
	# Stop the kvlds server and delete its (defunct) socket.
	kvlds_stop "${kvlds_pidfile}" "0"
	rm "${kvlds_sock}"

	# Start kvlds with 5-byte keys; the server should be ok...
	kvlds_start "${kvlds_sock}" "${lbs_sock}" "0"		\
		"${kvlds_pidfile}" "-k 5" "0"			\
		"kvlds -k 5 ${_kvlds_check_large_description}"
	# ... but the test for 5-byte keys should fail.
	setup_check "test_kvlds ${_kvlds_check_large_description} 5-byte keys"
	${c_valgrind_cmd} "${test_kvlds}" "${kvlds_sock}"	\
	    2> "${s_basename}-k5.stderr"
	expected_exitcode 1 "$?" > "${c_exitfile}"
	# Stop the kvlds server and delete its (defunct) socket.
	kvlds_stop "${kvlds_pidfile}" "0"
	rm "${kvlds_sock}"

	# Clean up.
	lbs_stop "${lbs_pidfile}" "0"
	clean_storage
}

_kvlds_check_unclean() {
	_kvlds_check_unclean_description=$1

	# Ensure that we're starting with a clean storage directory.
	servers_check_leftover
	clean_storage

	# Start the servers.
	lbs_start "${lbs_sock}" "${stor}" "0" "${lbs_pidfile}"		\
		"${lbs_basic_read_delay}"				\
		"lbs ${_kvlds_check_unclean_description}"
	kvlds_start "${kvlds_sock}" "${lbs_sock}" "0"			\
		"${kvlds_pidfile}" "-v 104 -C 1024" "0"			\
		"kvlds ${_kvlds_check_unclean_description}"

	# Check that an unclean disconnect is handled appropriately.
	setup_check "test_kvlds ${_kvlds_check_unclean_description} clean"
	# First attempt; unclean
	( "${test_kvlds}" "${kvlds_sock}" &				\
		echo $! > "${testkvlds_pidfile}" ) 2>/dev/null
	"${msleep}" 100 && kill "$(cat "${testkvlds_pidfile}")"
	rm -f "${testkvlds_pidfile}"
	# Second attempt; unclean
	( "${test_kvlds}" "${kvlds_sock}" &				\
		echo $! > "${testkvlds_pidfile}" ) 2>/dev/null
	"${msleep}" 100 && kill "$(cat "${testkvlds_pidfile}")"
	rm -f "${testkvlds_pidfile}"
	# Third attempt; clean
	"${test_kvlds}" "${kvlds_sock}"
	echo "$?" > "${c_exitfile}"

	# Stop the kvlds server.
	kvlds_stop "${kvlds_pidfile}" "0"

	# Clean up
	lbs_stop "${lbs_pidfile}" "0"
	clean_storage
}

_kvlds_check_killing() {
	_kvlds_check_killing_description=$1

	# Ensure that we're starting with a clean storage directory.
	servers_check_leftover
	clean_storage

	# Start the lbs server.
	lbs_start "${lbs_sock}" "${stor}" "0" "${lbs_pidfile}"		\
		"${lbs_basic_read_delay}"				\
		"lbs ${_kvlds_check_killing_description}"

	# Check that killing KVLDS can't break it.
	for i in 1 2 3 ; do
		# Begin the server...
		kvlds_start "${kvlds_sock}" "${lbs_sock}" "0"		\
			"${kvlds_pidfile}" "-v 104" "0"			\
			"kvlds ${_kvlds_check_killing_description} $i"
		# ... and test_kvlds...
		"${test_kvlds}" "${kvlds_sock}"
		# ... but kill the server.
		"${msleep}" 100 && kill "$(cat "${kvlds_pidfile}")"
		# Clean up.
		rm -f "${kvlds_pidfile}"
		rm -f "${kvlds_sock}"
	done
	# Final time; don't kill it.
	kvlds_start "${kvlds_sock}" "${lbs_sock}" "0"			\
		"${kvlds_pidfile}" "-v 104" "0"				\
		"kvlds ${_kvlds_check_killing_description} kvlds"
	setup_check "test_kvlds ${_kvlds_check_killing_description} after"
	"${test_kvlds}" "${kvlds_sock}"
	echo "$?" > "${c_exitfile}"

	# Stop the kvlds server.
	kvlds_stop "${kvlds_pidfile}" "0"

	# Clean up
	lbs_stop "${lbs_pidfile}" "0"
	clean_storage
}

_kvlds_check_nontwo() {
	_kvlds_check_nontwo_description=$1

	# Ensure that we're starting with a clean storage directory.
	servers_check_leftover
	clean_storage

	# Start LBS with non-power-of-2 byte pages.
	for X in 1000 1023 1025 1100; do
		# Start the lbs server.
		lbs_start "${lbs_sock}" "${stor}" "0" "${lbs_pidfile}"	\
			"${lbs_basic_read_delay}"			\
			"lbs ${_kvlds_check_nontwo_description} ${X}"	\
			"${X}"
		# Start the kvlds server.
		kvlds_start "${kvlds_sock}" "${lbs_sock}" "0"		\
			"${kvlds_pidfile}" "-v 104 -C 1024" "0"		\
			"kvlds ${_kvlds_check_nontwo_description} ${X}"
		# Test it.
		setup_check						\
			"test_kvlds ${_kvlds_check_nontwo_description} ${X}" 0
		"${test_kvlds}" "${kvlds_sock}"
		echo "$?" > "${c_exitfile}"

		# Stop the kvlds server.
		kvlds_stop "${kvlds_pidfile}" "0"
		# Clean up
		lbs_stop "${lbs_pidfile}" "0"
		clean_storage
	done
}

### Actual command
scenario_cmd() {
	# Check kvlds with the server in single-connection mode.
	_kvlds_check_basic 1 "basic single"
	_kvlds_check_basic 0 "basic non-single"

	# Check by running the test twice.
	_kvlds_check_repeated "repeated"

	# Check with keys and values which are too large.
	_kvlds_check_large "key & values too large"

	# Check that an unclean disconnect is handled appropriately.
	_kvlds_check_unclean "unclean"

	# Check that killing kvlds can't break it.
	_kvlds_check_killing "killing"

	# Check with non-power-of-2 lbs byte pages.
	_kvlds_check_nontwo "non-power-of-2"
}
