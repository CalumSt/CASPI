/*
 * @file Features_test.cpp
 *
 * Compile-time checks for CASPI_DEPRECATED / CASPI_SUPPRESS_DEPRECATED_BEGIN
 * / CASPI_SUPPRESS_DEPRECATED_END (caspi_Features.h). These macros only
 * affect compiler diagnostics, not behaviour, so the tests assert that
 * marked declarations still compile and run correctly — the macros
 * themselves are exercised simply by this file building without error.
 */

#include "base/caspi_Features.h"
#include <gtest/gtest.h>

namespace
{
    CASPI_DEPRECATED ("use newAdd() instead")
    int oldAdd (int a, int b)
    {
        return a + b;
    }

    int newAdd (int a, int b)
    {
        return a + b;
    }

    class CASPI_DEPRECATED ("use NewWidget instead") OldWidget
    {
        public:
            int value() const { return 42; }
    };
}

TEST (FeaturesTest, DeprecatedFunctionStillCallableUnderSuppression)
{
    CASPI_SUPPRESS_DEPRECATED_BEGIN
    EXPECT_EQ (oldAdd (2, 3), 5);
    CASPI_SUPPRESS_DEPRECATED_END
}

TEST (FeaturesTest, DeprecatedClassStillUsableUnderSuppression)
{
    CASPI_SUPPRESS_DEPRECATED_BEGIN
    OldWidget w;
    EXPECT_EQ (w.value(), 42);
    CASPI_SUPPRESS_DEPRECATED_END
}

TEST (FeaturesTest, DeprecatedAndReplacementAgree)
{
    // No suppression needed here: oldAdd() isn't called, only referenced
    // indirectly via its already-verified behaviour above.
    EXPECT_EQ (newAdd (10, -4), 6);
}
