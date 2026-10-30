#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/framework/log.sh
. ${SCRIPT_DIR}/framework/config.sh

cd $SCRIPT_DIR

TEST_COUNT=0
PASS_COUNT=0
FAIL_COUNT=0

for test in $(find tests/$1 -name test.sh | sort); do
    if [ ! -x $test ]; then
        log_warn -m "Script $test hasn't executable bit set - skipping!"

        continue
    fi

    testName=$(dirname $test)
    testName=${testName#"tests/"}

    TEST_COUNT=$(( TEST_COUNT + 1))

    printf "[ %-3d ] %-64s " "$TEST_COUNT" "$testName"

    bash -Ee $test

    rc=$?
    if [ $rc -eq $TEST_RC_SUCCESS ]; then
        printf "%-10s\n" "Success"
        PASS_COUNT=$(( PASS_COUNT + 1 ))

    elif [ $rc -eq $TEST_RC_SKIP ]; then
        printf "%-10s\n" "Skipped"

    else
        printf "%-10s\n" "FAIL"
        FAIL_COUNT=$(( FAIL_COUNT + 1 ))
    fi
done

echo -e "\n###########################################"
printf "Testing completed PASSED: %-3d, FAILED: %-3d\n" "$PASS_COUNT" "$FAIL_COUNT"
echo -e "###########################################"

if [ $FAIL_COUNT -gt 0 ]; then
    exit 1
fi

exit 0