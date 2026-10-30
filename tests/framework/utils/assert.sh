__assert_indent="\\t\\t\\t"

function utils_assert_equals() {
    local actual=$1; shift
    local expected=$1; shift

    if [ "x$actual" != "x$expected" ]; then
        # TODO: Move it to test_assert_equals()
        test_fatal "$@: ${__assert_indent}FAILED (expected: '$expected' actual: '$actual')"

        return 1
    fi

    # TODO: Move it to test_assert_equals()
    test_log "$@: ${__assert_indent}OK (expected: '$expected', actual: '$actual')"

    return 0
}

function utils_assert_not_equals() {
    local actual=$1; shift
    local expected=$1; shift

    if [ "x$actual" == "x$expected" ]; then
        # TODO: Move it to test_assert_equals()
        test_fatal "$@: ${__assert_indent}FAILED (expected: != '$expected' actual: '$actual')"

        return 1
    fi

    # TODO: Move it to test_assert_equals()
    test_log "$@: ${__assert_indent}OK (expected: != '$expected', actual: '$actual')"

    return 0
}