#!/bin/sh

set -e

./test_network_ssl www.tarsnap.com

printf "\nGoing to try tests designed to fail.\n"
printf "(you can ignore the \"certificate verify failed\" messages.)\n\n"

! ./test_network_ssl wrong.host.badssl.com
! ./test_network_ssl self-signed.badssl.com
! ./test_network_ssl expired.badssl.com
! ./test_network_ssl untrusted-root.badssl.com
