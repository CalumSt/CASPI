/*************************************************************************
*  .d8888b.                             d8b
* d88P  Y88b                            Y8P
* 888    888
* 888         8888b.  .d8888b  88888b.  888
* 888            "88b 88K      888 "88b 888
* 888    888 .d888888 "Y8888b. 888  888 888
* Y88b  d88P 888  888      X88 888 d88P 888
*  "Y8888P"  "Y888888  88888P' 88888P"  888
*                              888
*                              888
*                              888
*
* @file caspi_Expected.h Graceful error handling.
* @author CS Islay
* @brief Provides an implementation of the expected<T, E> type that is C++11 compatible.
*        This type is used to represent a value that may either be a valid result of type T
*        or an error of type E.
*        It is similar to std::expected introduced in C++23, probably less robust.
*        Note that it is written in a format similar to the standard library.
************************************************************************/

#ifndef CASPI_EXPECTED_H
#define CASPI_EXPECTED_H

#include "base/caspi_Assert.h"
#include "base/caspi_Features.h"
#include "base/caspi_Platform.h"
#include "base/caspi_Traits.h"

#include <type_traits>
#include <utility>

namespace CASPI
{

    // clang-format off
    /**
     * @struct unexpect_tag_t
     * @brief Tag type used to construct an `expected` instance holding an error.
     */
    struct unexpect_tag_t {};

    /**
     * @var unexpect
     * @brief Global constant to indicate error construction.
     */
    constexpr unexpect_tag_t unexpect{};

    /**
     * @struct in_place_t
     * @brief Tag type for in-place construction.
     */
    struct in_place_t { explicit in_place_t() = default; };

    /**
     * @var in_place
     * @brief Global constant for in-place construction.
     */
    constexpr in_place_t in_place{};
    // clang-format on

    template <typename T, typename E>
    class expected;

    /**
     * @brief Constructs a successful expected object with a copy of the value.
     * @tparam T The type of the value.
     * @tparam E The error type.
     * @param value The value to store.
     * @return An `expected<T, E>` object containing the value.
     */
    template <typename T, typename E = void>
    expected<T, E> make_expected (const T& value)
    {
        return expected<T, E> { value };
    }

    /**
 * @brief Constructs a successful expected object with a moved value.
 * @tparam T The type of the value.
 * @tparam E The error type.
 * @param value The value to move.
 * @return An `expected<T, E>` object containing the moved value.
 */
    template <typename T, typename E = void>
    expected<T, E> make_expected (T&& value)
    {
        return expected<T, E> { std::forward<T> (value) };
    }

    /**
 * @brief Constructs a failed expected object with a copy of the error.
 * @tparam T The type of the value.
 * @tparam E The error type.
 * @param error The error to store.
 * @return An `expected<T, E>` object containing the error.
 */
    template <typename T, typename E>
    expected<T, E> make_unexpected (const E& error)
    {
        return expected<T, E> { unexpect, error };
    }

    /**
     * @brief Constructs a failed expected object with a moved error.
     * @tparam T The type of the value.
     * @tparam E The error type.
     * @param error The error to move.
     * @return An `expected<T, E>` object containing the moved error.
     */
    template <typename T, typename E>
    expected<T, E> make_unexpected (E&& error)
    {
        return expected<T, E> { unexpect, std::forward<T> (error) };
    }

    /**
     * @class expected
     * @brief Represents a value or an error, similar to `std::expected` in C++23 but C++11 compatible.
     * @tparam T The value type.
     * @tparam E The error type.
     */
    template <typename T, typename E>
    class expected
    {
            CASPI_STATIC_ASSERT (std::is_nothrow_destructible<T>::value,
                                 "T must be noexcept destructible");
            CASPI_STATIC_ASSERT (std::is_nothrow_destructible<E>::value,
                                 "E must be noexcept destructible");

            union
            {
                    T _val;
                    E _err;
            };
            bool _has_value;

        public:
            /**
            * @brief Swaps two expected objects.
            */
            friend void swap (expected& lhs, expected& rhs) noexcept (
                CASPI_IS_NOTHROW_SWAPPABLE (T) && CASPI_IS_NOTHROW_SWAPPABLE (E) && std::is_nothrow_move_constructible<T>::value && std::is_nothrow_move_constructible<E>::value)
            {
                if (lhs._has_value && rhs._has_value)
                {
                    using std::swap;
                    swap (lhs._val, rhs._val);
                }
                else if (! lhs._has_value && ! rhs._has_value)
                {
                    swap (lhs._err, rhs._err);
                }
                else
                {
                    // Different states: move one into temp, reconstruct both
                    /*
                        We can’t directly assign between value/error because only one is active in
                       the union at a time.

                        So we use:

                        temporary move (to hold the non-active side),

                        destroy the active members,

                        reconstruct using placement new.

                        After this block:

                        has now holds the original error from not_has

                        not_has now holds the original value from has
                    */
                    expected* has     = lhs._has_value ? &lhs : &rhs;
                    expected* not_has = lhs._has_value ? &rhs : &lhs;

                    if (has->_has_value)
                    {
                        E tmp = std::move (not_has->_err);
                        not_has->~expected();
                        new (not_has) expected (std::move (has->_val));
                        has->~expected();
                        new (has) expected (unexpect, std::move (tmp));
                    }
                    else
                    {
                        T tmp = std::move (has->_val);
                        has->~expected();
                        new (has) expected (unexpect, std::move (not_has->_err));
                        not_has->~expected();
                        new (not_has) expected (std::move (tmp));
                    }
                }
            }

            /**
* @brief Copy constructor
* @param other Other expected
*/
            expected(const expected &other) : _has_value(other._has_value) {
                if (_has_value)
                    new(&_val) T(other._val);
                else
                    new(&_err) E(other._err);
            }

            /**
         * @brief Move constructor for expected
         * @param v other expected
         */
            explicit expected(T &&v) : _has_value(true) {
                new(&_val) T(std::move(v));
            }

            /**
         * @brief Move constructor for unexpected
         * @param e Error expected
         */
            expected(unexpect_tag_t, E &&e) : _has_value(false) {
                new(&_err) E(std::move(e));
            }

            /**
         * @brief Construct a new expected object in place
         */
            template<typename... Args>
            explicit expected(CASPI::in_place_t, Args &&... args)
                : _has_value(true) {
                new(&_val) T(std::forward<Args>(args)...);
            }

            /**
         * @brief Construct a new unexpected object in place
         */
            template<typename... Args>
            explicit expected(unexpect_tag_t, Args &&... args)
                : _has_value(false) {
                new(&_err) E(std::forward<Args>(args)...);
            }

            /**
         * @brief Move constructor for generic expected
         * @param other other expected object
         */
            expected(expected &&other) noexcept : _has_value(other._has_value) {
                if (_has_value)
                    new(&_val) T(std::move(other._val));
                else
                    new(&_err) E(std::move(other._err));
            }

            /**
            * @brief Constructs a success `expected` from a value.
            */
            explicit expected(const T &val) : _has_value(true) {
                new(&_val) T(val);
            }

            /**
            * @brief Constructs a failure `expected` from an error.
            */
            expected(unexpect_tag_t, const E &err) : _has_value(false) {
                new(&_err) E(err);
            }

            ~expected() {
                if (_has_value)
                    _val.~T();
                else
                    _err.~E();
            }

            /**
            * @brief Conversion operator to check if the expected contains a value.
            * @return true if the expected contains a value, false if it contains an error.
            */
            explicit operator bool() const noexcept { return _has_value; }

            /**
             * @brief Equality comparison operator.
             * @param lhs The left-hand side expected object.
             * @param rhs The right-hand side expected object.
             * @return true if both expected objects contain the same value or the same error.
             */
            friend bool operator==(const expected &lhs, const expected &rhs) noexcept {
                if (lhs._has_value != rhs._has_value)
                    return false;
                if (lhs._has_value)
                    return lhs._val == rhs._val;
                return lhs._err == rhs._err;
            }

            /**
             * @brief Inequality comparison operator.
             * @param lhs The left-hand side expected object.
             * @param rhs The right-hand side expected object.
             * @return true if the two expected objects are not equal.
             */
            friend bool operator!=(const expected &lhs, const expected &rhs) noexcept {
                return !(lhs == rhs);
            }

            /**
             * @brief Copy assignment operator.
             *        Provides strong exception safety via copy-and-swap idiom.
             * @param other The expected instance to copy from.
             * @return Reference to this expected after assignment.
             */
            expected &operator=(const expected &other) {
                if (this != &other) {
                    expected temp(other); // May throw
                    swap(*this, temp); // Never throws
                }
                return *this;
            }

            /**
             * @brief Move assignment operator.
             *        Provides strong exception safety via move-and-swap idiom.
             * @param other The expected instance to move from.
             * @return Reference to this expected after assignment.
             */
            expected &operator=(expected &&other) noexcept {
                if (this != &other) {
                    expected temp(std::move(other)); // Should not throw
                    swap(*this, temp); // Never throws
                }
                return *this;
            }

            /**
             * @brief Applies the given function to the contained value if present, propagates error otherwise.
             * @tparam F A callable taking `const T&` and returning an expected-like result.
             * @param f The function to apply to the contained value.
             * @return The result of applying `f` if value is present; otherwise an expected with the same error.
             */
            template <typename F>
            auto and_then (F&& f) const&
            {
                using result_t = decltype (f (std::declval<const T&>()));
                if (_has_value)
                    return f (_val);
                else
                    return result_t (unexpect, _err);
            }

            /**
             * @brief Applies a function to the contained value and wraps the result in a new expected.
             * @tparam F A callable taking `const T&` and returning U.
             * @param f The function to apply.
             * @return expected<U, E> with transformed value or the original error.
             */
            template<typename F>
            auto map(F &&f) const & {
                using U = decltype (f(std::declval<const T &>()));
                if (_has_value)
                    return expected<U, E>(f(_val));
                else
                    return expected<U, E> (unexpect, _err);
            }

            /**
             * @brief Calls a recovery function if the expected contains an error.
             * @tparam F A callable taking `const E&` and returning an expected-like result.
             * @param f The recovery function.
             * @return The original value if present, otherwise result of applying `f` to the error.
             */
            template<typename F>
            auto or_else(F &&f) const & {
                using result_t = decltype (f(std::declval<const E &>()));
                if (!_has_value)
                    return f(_err);
                else
                    return result_t (_val);
            }

            /**
             * @brief Rvalue overload of map; moves the value if present and applies `f`.
             * @tparam F A callable taking `T&&` and returning U.
             * @param f The function to apply.
             * @return expected<U, E> with transformed value or the original error.
             */
            template<typename F>
            auto map(F &&f) && {
                using U = decltype (f(std::move(_val)));
                if (_has_value)
                    return expected<U, E>(f(std::move(_val)));
                return expected<U, E>(unexpect, std::move(_err));
            }

            /**
             * @brief Rvalue overload of and_then; moves the value if present and applies `f`.
             * @tparam F A callable taking `T&&` and returning an expected-like result.
             * @param f The function to apply.
             * @return The result of applying `f`, or an expected with the moved error.
             */
            template<typename F>
            auto and_then(F &&f) && {
                using result_t = decltype (f(std::move(_val)));
                if (_has_value)
                    return f(std::move(_val));
                return result_t(unexpect, std::move(_err));
            }

            /**
             * @brief Rvalue overload of or_else; moves the error if present and applies `f`.
             * @tparam F A callable taking `E&&` and returning an expected-like result.
             * @param f The recovery function.
             * @return The original value if present, otherwise result of applying `f` to the moved error.
             */
            template<typename F>
            auto or_else(F &&f) && {
                using result_t = decltype (f(std::move(_err)));
                if (!_has_value)
                    return f(std::move(_err));
                return result_t(std::move(_val));
            }

            /**
 * @brief Checks if the expected contains a valid value.
 * @return true if the expected holds a value; false if it holds an error.
 */
            CASPI_NO_DISCARD bool has_value() const
            {
                return _has_value;
            }

            /**
             * @brief Checks if the expected contains an error.
             * @return true if the expected holds an error; false if it holds a value.
             */
            CASPI_NO_DISCARD bool has_error() const
            {
                return ! _has_value;
            }

            /**
             * @brief Retrieves the contained value.
             * @return A const reference to the value.
             * @throws Asserts if the expected holds an error instead of a value.
             */
            const T& value() const
            {
                CASPI_ASSERT (_has_value, "Expected does not hold a value.");
                return _val;
            }

            /**
             * @brief Retrieves the contained error.
             * @return A const reference to the error.
             * @throws Asserts if the expected holds a value instead of an error.
             */
            const E& error() const
            {
                CASPI_ASSERT (! _has_value, "Expected does not hold an error.");
                return _err;
            }
    };

    template<typename T, typename E>
    class noexcept_expected {
        CASPI_STATIC_ASSERT(std::is_nothrow_destructible<T>::value,
                            "T must be noexcept destructible");
        CASPI_STATIC_ASSERT(std::is_nothrow_destructible<E>::value,
                            "E must be noexcept destructible");

        CASPI_STATIC_ASSERT(std::is_nothrow_move_constructible<T>::value,
                            "T must be noexcept move constructible");
        CASPI_STATIC_ASSERT (std::is_nothrow_move_constructible<E>::value, "E must be noexcept move constructible");

            CASPI_STATIC_ASSERT (std::is_nothrow_move_assignable<T>::value, "T must be noexcept move assignable");
            CASPI_STATIC_ASSERT (std::is_nothrow_move_assignable<E>::value, "E must be noexcept move assignable");

            union
            {
                    T _val;
                    E _err;
            };
            bool _has_value;

        public:
            explicit operator bool() const noexcept { return _has_value; }

            // Constructors
            explicit noexcept_expected (const T& val) noexcept (std::is_nothrow_copy_constructible<T>::value)
                : _has_value (true)
            {
                new (&_val) T (val);
            }

            explicit noexcept_expected (T&& val) noexcept (std::is_nothrow_move_constructible<T>::value)
                : _has_value (true)
            {
                new (&_val) T (std::move (val));
            }

            explicit noexcept_expected (unexpect_tag_t, const E& err) noexcept (std::is_nothrow_copy_constructible<E>::value)
                : _has_value (false)
            {
                new (&_err) E (err);
            }

            explicit noexcept_expected (unexpect_tag_t, E&& err) noexcept (std::is_nothrow_move_constructible<E>::value)
                : _has_value (false)
            {
                new (&_err) E (std::move (err));
            }

            // Move constructor
            noexcept_expected (noexcept_expected&& other) noexcept
                : _has_value (other._has_value)
            {
                if (_has_value)
                    new (&_val) T (std::move (other._val));
                else
                    new (&_err) E (std::move (other._err));
            }

            // Move assignment operator
            noexcept_expected& operator= (noexcept_expected&& other) noexcept
            {
                if (this != &other)
                {
                    this->~noexcept_expected();
                    new (this) noexcept_expected (std::move (other));
                }
                return *this;
            }

            // Destructor
            ~noexcept_expected() noexcept
            {
                if (_has_value)
                    _val.~T();
                else
                    _err.~E();
            }

            // Accessors
            const T& value() const noexcept
            {
                // You could do your own assert or error handling, but must not throw
                return _val;
            }

            const E& error() const noexcept
            {
                return _err;
            }

            bool has_value() const noexcept { return _has_value; }
            bool has_error() const noexcept { return ! _has_value; }

            // Swap
            friend void swap (noexcept_expected& lhs, noexcept_expected& rhs) noexcept
            {
                if (lhs._has_value && rhs._has_value)
                    std::swap (lhs._val, rhs._val);
                else if (! lhs._has_value && ! rhs._has_value)
                    std::swap (lhs._err, rhs._err);
                else
                {
                    // Swap with careful moves and placement new
                    noexcept_expected* has     = lhs._has_value ? &lhs : &rhs;
                    noexcept_expected* not_has = lhs._has_value ? &rhs : &lhs;

                    if (has->_has_value)
                    {
                        E tmp = std::move (not_has->_err);
                        not_has->~noexcept_expected();
                        new (not_has) noexcept_expected (std::move (has->_val));
                        has->~noexcept_expected();
                        new (has) noexcept_expected (unexpect, std::move (tmp));
                    }
                    else
                    {
                        T tmp = std::move (has->_val);
                        has->~noexcept_expected();
                        new (has) noexcept_expected (unexpect, std::move (not_has->_err));
                        not_has->~noexcept_expected();
                        new (not_has) noexcept_expected (std::move (tmp));
                    }
                }
            }
    };

} // namespace CASPI

#endif