#include "core/caspi_Expected.h"
#include <gtest/gtest.h>

#include <string>
#include <memory>

TEST(ExpectedTest, ConstructsWithValue) {
    CASPI::expected<int, std::string> e(42);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e.value(), 42);
}

TEST(ExpectedTest, ConstructsWithError) {
    CASPI::expected<int, std::string> e(CASPI::unexpect, std::string("fail"));
    EXPECT_FALSE(e.has_value());
    EXPECT_EQ(e.error(), "fail");
}

TEST(ExpectedTest, AssignValue) {
    CASPI::expected<int, std::string> a(5);
    CASPI::expected<int, std::string> b = a;
    EXPECT_EQ(b.value(), 5);
}

TEST(ExpectedTest, CopyValue) {
    CASPI::expected<int, std::string> a(123);
    CASPI::expected<int, std::string> b(a);
    EXPECT_TRUE(b.has_value());
    EXPECT_EQ(b.value(), 123);
}

TEST(ExpectedTest, MoveValue) {
    CASPI::expected<std::unique_ptr<int>, std::string> e(std::make_unique<int>(5));
    CASPI::expected<std::unique_ptr<int>, std::string> moved(std::move(e));
    EXPECT_TRUE(moved.has_value());
    EXPECT_EQ(*moved.value(), 5);
}

namespace CASPI::test {
    struct ThrowOnCopy {
        ThrowOnCopy() {}

        ThrowOnCopy(const ThrowOnCopy&) {
            throw std::runtime_error("Copy constructor threw");
        }

        ThrowOnCopy& operator=(const ThrowOnCopy&) {
            throw std::runtime_error("Copy assignment threw");
        }

        ThrowOnCopy(ThrowOnCopy&&) noexcept = default;
        ThrowOnCopy& operator=(ThrowOnCopy&&) noexcept = default;
    };

    struct ThrowOnSwap {
        int value;

        friend void swap(ThrowOnSwap&, ThrowOnSwap&) {
            throw std::runtime_error("Swap failed");
        }
    };
}

TEST(ExpectedTest, MoveConstructionSafe) {
    CASPI::expected<CASPI::test::ThrowOnCopy, std::string> a(CASPI::test::ThrowOnCopy{});
    EXPECT_TRUE(a.has_value());
}

TEST(ExpectedTest, CopyConstructorThrows) 
{
    using  throw_on_copy = CASPI::expected<CASPI::test::ThrowOnCopy, std::string>;
    throw_on_copy a(CASPI::test::ThrowOnCopy{});
    EXPECT_THROW(
    {
        throw_on_copy b(a);  // Should throw
    },
     std::runtime_error);
}

TEST(ExpectedTest, CopyAssignmentThrowsButLeavesOriginalIntact) {
    CASPI::expected<CASPI::test::ThrowOnCopy, std::string> good(CASPI::test::ThrowOnCopy{});
    CASPI::expected<CASPI::test::ThrowOnCopy, std::string> copy_source(CASPI::test::ThrowOnCopy{});

    try {
        good = copy_source;  // Copy constructor will throw
        FAIL() << "Expected exception not thrown";
    } catch (const std::runtime_error& e) {
        // still OK — original object should be valid
        EXPECT_TRUE(good.has_value());
    }
}

TEST(ExpectedTest, SwapThrows) {
    CASPI::expected<CASPI::test::ThrowOnSwap, std::string> a(CASPI::test::ThrowOnSwap{});
    CASPI::expected<CASPI::test::ThrowOnSwap, std::string> b(CASPI::test::ThrowOnSwap{});

    EXPECT_THROW(swap(a, b), std::runtime_error);
}

TEST(ExpectedTest, EqualityOperator) {
    CASPI::expected<int, int> e1(42), e2(42), e3(CASPI::unexpect, -1), e4(CASPI::unexpect, -1);
    EXPECT_TRUE(e1 == e2);
    EXPECT_TRUE(e3 == e4);
    EXPECT_FALSE(e1 == e3);
}

TEST(ExpectedMonadic, AndThenAppliesFunctionOnSuccess) {
    CASPI::expected<int, const char*> e(42);

    auto result = e.and_then([](int x) {
        return CASPI::expected<std::string, const char*>("Value is " + std::to_string(x));
    });

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "Value is 42");
}

TEST(ExpectedMonadic, AndThenSkipsFunctionOnError) {
    CASPI::expected<int, const char*> e(CASPI::unexpect, "Something went wrong");

    auto result = e.and_then([](int x) {
        return CASPI::expected<std::string, const char*>("Value is " + std::to_string(x));
    });

    EXPECT_FALSE(result.has_value());
    EXPECT_STREQ(result.error(), "Something went wrong");
}

TEST(ExpectedMonadic, AndThenRvalueMovesValue) {
    CASPI::expected<std::string, const char*> e("hello");

    auto result = std::move(e).and_then([](std::string&& str) {
        return CASPI::expected<size_t, const char*>(str.size());
    });

    static_assert(std::is_same<decltype(result), CASPI::expected<size_t, const char*>>::value,
                  "and_then should transform expected<std::string, E> to expected<size_t, E>");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5);
}

TEST(ExpectedMonadic, AndThenRvaluePropagatesError) {
    CASPI::expected<std::string, const char*> e(CASPI::unexpect, "failure");

    auto result = std::move(e).and_then([](std::string&& str) {
        return CASPI::expected<size_t, const char*>(str.size());  // should not be called
    });

    static_assert(std::is_same<decltype(result), CASPI::expected<size_t, const char*>>::value,
                  "and_then should preserve error type and transform value type");

    ASSERT_TRUE(result.has_error());
    EXPECT_STREQ(result.error(), "failure");
}

TEST(ExpectedMonadic, MapRvalueTransformsValue) {
    CASPI::expected<std::string, const char*> e(std::string("hello"));

    auto result = std::move(e).map([](std::string const& s) {
        return s + " world";
    });

    static_assert(std::is_same_v<decltype(result), CASPI::expected<std::string, const char*>>,
                  "Return type must be expected<std::string, const char*>");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "hello world");
}

TEST(ExpectedMonadic, MapRvaluePreservesError) {
    CASPI::expected<std::string, const char*> e(CASPI::unexpect, "error");

    auto result = std::move(e).map([](std::string const& s) {
        return s + " world";  // Should not be called
    });

    static_assert(std::is_same_v<decltype(result), CASPI::expected<std::string, const char*>>,
                  "Return type must be expected<std::string, const char*>");

    ASSERT_TRUE(result.has_error());
    EXPECT_STREQ(result.error(), "error");
}

TEST(ExpectedMonadic, MapConstLvalueTransformsValue) {
    const CASPI::expected<int, const char*> e(42);

    auto result = e.map([](int v) {
        return std::to_string(v);
    });

    static_assert(std::is_same_v<decltype(result), CASPI::expected<std::string, const char*>>,
                  "map should transform CASPI::expected<int, E> to CASPI::expected<string, E>");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "42");
}

TEST(ExpectedMonadic, MapConstLvaluePropagatesError) {
    const CASPI::expected<int, const char*> e(CASPI::unexpect, "error");

    auto result = e.map([](int v) {
        return std::to_string(v);  // Should not be called
    });

    static_assert(std::is_same_v<decltype(result), CASPI::expected<std::string, const char*>>,
                  "map should preserve the error type");

    ASSERT_TRUE(result.has_error());
    EXPECT_STREQ(result.error(), "error");
}

TEST(ExpectedMonadic, OrElseRvalueCallsOnError) {
    CASPI::expected<int, std::string> e(CASPI::unexpect, "error occurred");

    auto result = std::move(e).or_else([](std::string&& err) {
        // Transform error into a new expected<int, std::string> holding a value
        return CASPI::expected<int, std::string>(42);
    });

    static_assert(std::is_same<decltype(result), CASPI::expected<int, std::string>>::value,
                  "or_else should return same expected type");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST(ExpectedMonadic, OrElseRvalueReturnsValueWithoutCalling) {
    CASPI::expected<int, std::string> e(10);

    bool called = false;

    auto result = std::move(e).or_else([&called](std::string&&) {
        called = true;
        return CASPI::expected<int, std::string>(-1);
    });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 10);
    EXPECT_FALSE(called);
}

TEST(ExpectedMonadic, OrElseConstLvalueCallsOnError) {
    CASPI::expected<int, std::string> e(CASPI::unexpect, "error occurred");

    auto result = e.or_else([](const std::string& err) {
        // Convert error string length to expected<int, std::string> success value
        return CASPI::expected<int, std::string>(static_cast<int>(err.size()));
    });

    static_assert(std::is_same<decltype(result), CASPI::expected<int, std::string>>::value,
                  "or_else should return same expected type");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 14);  // length of "error occurred"
}

TEST(ExpectedMonadic, OrElseConstLvalueReturnsValueWithoutCalling) {
    CASPI::expected<int, std::string> e(10);

    bool called = false;

    auto result = e.or_else([&called](const std::string&) {
        called = true;
        return CASPI::expected<int, std::string>(-1);
    });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 10);
    EXPECT_FALSE(called);
}

CASPI::expected<int, std::string> divide(int numerator, int denominator) {
    if (denominator == 0)
        return CASPI::expected<int, std::string>(CASPI::unexpect, "division by zero");
    else
        return CASPI::expected<int, std::string>(numerator / denominator);
}

TEST(ExpectedMonadic, AndThenOrElseChain) {
    // Start with a successful expected
    CASPI::expected<int, std::string> e = divide(10, 2);

    auto result = e
        .and_then([](int val) {
            // Multiply by 2 and return new expected
            return CASPI::expected<int, std::string>(val * 2);
        })
        .or_else([](const std::string& err) {
            // Recover from error by returning default value
            return CASPI::expected<int, std::string>(42);
        });

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 10);  // 10/2 = 5, then *2 = 10

    // Now test with error in initial expected
    CASPI::expected<int, std::string> e_err = divide(10, 0);

    auto result_err = e_err
        .and_then([](int val) {
            // This lambda should NOT be called because e_err has error
            return CASPI::expected<int, std::string>(val * 2);
        })
        .or_else([](const std::string& err) {
            // Recover by returning 42
            return CASPI::expected<int, std::string>(42);
        });

    EXPECT_TRUE(result_err.has_value());
    EXPECT_EQ(result_err.value(), 42);
}

TEST(ExpectedMonadic, ChainedAndThenOrElseSkipsAfterError) {
    // Start with an expected holding a value
    CASPI::expected<int, std::string> e(10);

    auto result = e
        .and_then([](int v) {
            // First and_then adds 1 -> 11
            return CASPI::expected<int, std::string>(v + 1);
        })
        .and_then([](int v) {
            // Second and_then introduces an error
            return CASPI::expected<int, std::string>(CASPI::unexpect, "error occurred");
        })
        .and_then([](int v) {
            // This and_then should be skipped because of previous error
            ADD_FAILURE() << "This and_then should not be called!";
            return CASPI::expected<int, std::string>(v * 10);
        })
        .or_else([](const std::string& err) {
            // Handle the error and recover by returning a new value
            EXPECT_STREQ(err.c_str(), "error occurred");
            return CASPI::expected<int, std::string>(42);
        });

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST(NoexceptExpectedTest, ConstructionAndAccessors)
{
    CASPI::noexcept_expected<int, std::string> val(123);
    EXPECT_TRUE(val.has_value());
    EXPECT_FALSE(val.has_error());
    EXPECT_EQ(val.value(), 123);

    CASPI::noexcept_expected<int, std::string> err(CASPI::unexpect, std::string("fail"));
    EXPECT_FALSE(err.has_value());
    EXPECT_TRUE(err.has_error());
    EXPECT_EQ(err.error(), "fail");
}

TEST(NoexceptExpectedTest, MoveConstructionAndAssignment)
{
    CASPI::noexcept_expected<int, std::string> val1(10);
    CASPI::noexcept_expected<int, std::string> val2(std::move(val1));
    EXPECT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 10);

    CASPI::noexcept_expected<int, std::string> err1(CASPI::unexpect, std::string("error"));
    CASPI::noexcept_expected<int, std::string> err2(std::move(err1));
    EXPECT_TRUE(err2.has_error());
    EXPECT_EQ(err2.error(), "error");

    CASPI::noexcept_expected<int, std::string> val3(20);
    val3 = std::move(val2);
    EXPECT_TRUE(val3.has_value());
    EXPECT_EQ(val3.value(), 10);

    CASPI::noexcept_expected<int, std::string> err3(CASPI::unexpect, std::string("old"));
    err3 = std::move(err2);
    EXPECT_TRUE(err3.has_error());
    EXPECT_EQ(err3.error(), "error");
}

TEST(NoexceptExpectedTest, Swap)
{
    CASPI::noexcept_expected<int, std::string> a(1);
    CASPI::noexcept_expected<int, std::string> b(2);
    swap(a, b);
    EXPECT_EQ(a.value(), 2);
    EXPECT_EQ(b.value(), 1);

    CASPI::noexcept_expected<int, std::string> c(CASPI::unexpect, std::string("c_err"));
    CASPI::noexcept_expected<int, std::string> d(CASPI::unexpect, std::string("d_err"));
    swap(c, d);
    EXPECT_EQ(c.error(), "d_err");
    EXPECT_EQ(d.error(), "c_err");

    CASPI::noexcept_expected<int, std::string> e(100);
    CASPI::noexcept_expected<int, std::string> f(CASPI::unexpect, std::string("fail"));
    swap(e, f);
    EXPECT_TRUE(e.has_error());
    EXPECT_EQ(e.error(), "fail");
    EXPECT_TRUE(f.has_value());
    EXPECT_EQ(f.value(), 100);
}

TEST(NoexceptExpectedTest, NoexceptProperties)
{
    static_assert(noexcept(CASPI::noexcept_expected<int, int>(42)), "Constructor should be noexcept");
    static_assert(noexcept(CASPI::noexcept_expected<int, int>(CASPI::unexpect, 1)), "Error constructor should be noexcept");
    static_assert(noexcept(CASPI::noexcept_expected<int, int>(std::move(CASPI::noexcept_expected<int, int>(42)))), "Move constructor should be noexcept");
    static_assert(noexcept(std::declval<CASPI::noexcept_expected<int, int>&>() = std::move(std::declval<CASPI::noexcept_expected<int, int>>())), "Move assignment should be noexcept");

    CASPI::noexcept_expected<int, int> a(1);
    CASPI::noexcept_expected<int, int> b(CASPI::unexpect, 2);
    static_assert(noexcept(swap(a, b)), "Swap should be noexcept");
}


enum class ResizeError { InvalidChannels, InvalidFrames, OutOfMemory };

TEST(ExpectedVoidTest, SuccessCase) {
    CASPI::expected<void, ResizeError> res; // default ctor => success
    EXPECT_TRUE(res.has_value());
}

TEST(ExpectedVoidTest, ErrorCaseConstruction) {
    CASPI::expected<void, ResizeError> res(CASPI::unexpect, ResizeError::InvalidChannels);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ResizeError::InvalidChannels);
}

TEST(ExpectedVoidTest, FactoryHelperErrorCase) {
    auto res = CASPI::make_unexpected<void, ResizeError>(ResizeError::InvalidFrames);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ResizeError::InvalidFrames);
}

TEST(ExpectedVoidTest, MoveSemanticsNonRealTimeSafe) {
    CASPI::expected<void, ResizeError, CASPI::NonRealTimeSafe> e1(
        CASPI::unexpect, ResizeError::OutOfMemory);
    auto e2 = std::move(e1);
    EXPECT_FALSE(e2.has_value());
    EXPECT_EQ(e2.error(), ResizeError::OutOfMemory);
}

TEST(ExpectedVoidTest, MoveSemanticsRealTimeSafe) {
    CASPI::expected<void, ResizeError, CASPI::RealTimeSafe> e1(
        CASPI::unexpect, ResizeError::OutOfMemory);
    auto e2 = std::move(e1);
    EXPECT_FALSE(e2.has_value());
    EXPECT_EQ(e2.error(), ResizeError::OutOfMemory);
}
