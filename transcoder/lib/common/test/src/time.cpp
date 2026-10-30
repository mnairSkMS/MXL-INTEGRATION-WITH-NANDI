#include <gtest/gtest.h>
#include "utpl/common/time.h"

#include "utpl/common/types/exception.h"

using namespace utpl;

// from utpl docker:
// date -d "2025-02-01T12:34:56.789Z" +%s%3N

TEST(TimeTest, FromIso8601_ValidDateTime) {
    std::string iso8601DateTime = "2025-02-01T12:34:56.789Z";
    Timestamp expectedTimestamp = 1738413296789;

    Timestamp result = Time::fromIso8601(iso8601DateTime);

    EXPECT_EQ(result, expectedTimestamp);
}

TEST(TimeTest, FromIso8601_InvalidDateTime) {
    std::string invalidIso8601DateTime = "invalid-date-time";

    EXPECT_THROW(Time::fromIso8601(invalidIso8601DateTime), utpl::Exception);
}

TEST(TimeTest, FromIso8601_NoFractionalSeconds) {
    std::string iso8601DateTime = "2025-02-01T12:34:56Z";
    Timestamp expectedTimestamp = 1738413296000; 

    Timestamp result = Time::fromIso8601(iso8601DateTime);

    EXPECT_EQ(result, expectedTimestamp);
}

TEST(TimeTest, FromIso8601_OnlyDate) {
    std::string iso8601DateTime = "2025-02-01";
    Timestamp expectedTimestamp = 1738368000000;

    Timestamp result = Time::fromIso8601(iso8601DateTime);

    EXPECT_EQ(result, expectedTimestamp);
}