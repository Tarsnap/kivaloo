#!/bin/sh

set -e

./test_https www.google.com /
./test_https s3.amazonaws.com /
./test_https www.tarsnap.com /kivaloo.html
./test_https wrong.host.badssl.com / || true
./test_https self-signed.badssl.com / || true
./test_https expired.badssl.com / || true
./test_https untrusted-root.badssl.com / || true
