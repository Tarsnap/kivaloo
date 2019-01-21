#!/bin/sh

set -e

./test_http www.google.com /
./test_http s3.amazonaws.com /
./test_http www.tarsnap.com /kivaloo.html
