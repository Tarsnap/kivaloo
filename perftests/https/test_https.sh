#!/bin/sh

failed=0
output=test_https.log

# Check for online access: can we access badssl.com?
if ! printf "GET / HTTP/1.0\r\n\r\n" | nc badssl.com 80 -w 10 > /dev/null 2>&1
then
	echo "SKIP TEST: can't access badssl.com; no internet access?"
	exit 0
fi

pass_test() {
	hostname=$1
	# Initialize to "/" if not specified
	path=${2:-"/"}

	printf "\n%s\n" "--- Trying to PASS on $hostname $path" >> $output

	./test_https $hostname $path >> $output
	ret=$?

	if [ "$ret" -ne "0" ]; then
		printf "Failed on $hostname!\n"
		failed=$((failed + 1))
	fi
}

fail_test() {
	hostname=$1
	# Initialize to "/" if not specified
	path=${2:-"/"}

	printf "%s\n" "--- Trying to FAIL on $hostname $path" >> $output

	./test_https $hostname $path 2>> $output
	ret=$?

	if [ "$ret" -eq "0" ]; then
		printf "Failed to fail on $hostname!\n"
		failed=$((failed + 1))
	fi
}

# Clear previous output
rm -f "${output}"

# We should pass these
pass_test www.google.com
pass_test s3.amazonaws.com
pass_test www.tarsnap.com
pass_test www.tarsnap.com /kivaloo.html

# We should fail these
fail_test wrong.host.badssl.com
fail_test self-signed.badssl.com
fail_test expired.badssl.com
fail_test untrusted-root.badssl.com

if [ "$failed" -ne 0 ]; then
	exit 1
fi
