#include <gtest/gtest.h>
#include <Utilities/caspi_Constants.h>
#include <Utilities/caspi_Assert.h>

TEST(UtilitiesTests, assert_test) {
    EXPECT_NO_THROW(CASPI_ASSERT(true,"If this has failed, sorry."));
}

TEST(UtilitiesTests, ConstantsTest) {
    constexpr double expected = 3.14159265358979323846;
    constexpr auto test = CASPI::Constants::PI<double>;
    EXPECT_EQ(test,expected);
}
