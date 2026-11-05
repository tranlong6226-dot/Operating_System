#!/bin/sh
set -e
cd "$(dirname "$0")/../.."
mkdir -p tests/host/build
CF="-Wall -Wextra -DconfigRTSFR_ENABLE=1 -Itests/host/stubs -Imylib"

gcc $CF tests/host/test_m3_resource.c mylib/rtsfr_resource.c \
    -o tests/host/build/test_m3 && tests/host/build/test_m3

gcc $CF -DRTSFR_HOST_TEST tests/host/test_m1_m3_integration.c \
    mylib/rtsfr_detect.c mylib/rtsfr_resource.c \
    -o tests/host/build/test_it && tests/host/build/test_it
