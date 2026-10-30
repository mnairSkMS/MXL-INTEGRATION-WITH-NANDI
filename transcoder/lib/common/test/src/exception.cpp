#include <gtest/gtest.h>

#include "utpl/common/types/exception.h"

// The code carried by an exception becomes the process exit status, so it must be the one it was
// constructed with rather than whatever the member happened to hold.
TEST(common_exception, keeps_the_code_it_was_constructed_with) {
    EXPECT_EQ(utpl::Exception(utpl::Error::PIPELINE_ERROR, "x").getCode(), utpl::Error::PIPELINE_ERROR);
    EXPECT_EQ(utpl::Exception("x").getCode(), utpl::Error::UNKNOWN_ERROR);
}

TEST(common_exception, declared_exceptions_carry_their_code) {
    EXPECT_EQ(utpl::InvalidArgumentException("x").getCode(), utpl::Error::INVALID_ARGUMENT);
    EXPECT_EQ(utpl::InvalidStateException("x").getCode(),    utpl::Error::INVALID_STATE);
    EXPECT_EQ(utpl::NotSupportedException("x").getCode(),    utpl::Error::NOT_SUPPORTED);
}

TEST(common_exception, code_survives_catch_by_base) {
    try {
        throw utpl::InvalidArgumentException("x");
    } catch (const utpl::Exception &ex) {
        EXPECT_EQ(ex.getCode(), utpl::Error::INVALID_ARGUMENT);
    }
}
