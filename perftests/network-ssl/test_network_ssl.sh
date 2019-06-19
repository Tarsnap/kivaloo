#!/bin/sh

failed=0
output=test_network_ssl_stderr.log

# Check for online access: can we access badssl.com?
if ! printf "GET / HTTP/1.0\r\n\r\n" | nc badssl.com 80 -w 10 > /dev/null 2>&1
then
	echo "SKIP TEST: can't access badssl.com; no internet access?"
	exit 0
fi

pass_test() {
	hostname=$1

	./test_network_ssl $hostname
	ret=$?

	if [ "$ret" -ne "0" ]; then
		printf "Failed on $hostname!\n"
		failed=$((failed + 1))
	fi
}

fail_test() {
	hostname=$1

	./test_network_ssl $hostname 2>> $output
	ret=$?

	if [ "$ret" -eq "0" ]; then
		printf "Failed to fail on $hostname!\n"
		failed=$((failed + 1))
	fi
}

# Clear previous output
rm -f "${output}"

# We should pass this
pass_test www.tarsnap.com

# We should fail these
fail_test wrong.host.badssl.com
fail_test self-signed.badssl.com
fail_test expired.badssl.com
fail_test untrusted-root.badssl.com

if [ "$failed" -ne 0 ]; then
	exit 1
fi
