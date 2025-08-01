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
    struct unexpect_tag_t {};
    constexpr unexpect_tag_t unexpect{};

    struct in_place_t { explicit in_place_t() = default; };
    constexpr in_place_t in_place{};
    // clang-format on

    template <typename T, typename E>
    class expected;

    // Factory for success
    template <typename T, typename E = void>
    expected<T, E> make_expected (const T& value)
    {
        return expected<T, E> { value };
    }

    template <typename T, typename E = void>
    expected<T, E> make_expected (T&& value)
    {
        return expected<T, E> { std::forward<T> (value) };
    }

    // Factory for failure
    template <typename T, typename E>
    expected<T, E> make_unexpected (const E& error)
    {
        return expected<T, E> { unexpect, error };
    }

    template <typename T, typename E>
    expected<T, E> make_unexpected (E&& error)
    {
        return expected<T, E> { unexpect, std::forward<T> (error) };
    }

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
            explicit operator bool() const noexcept { return _has_value; }

            friend bool operator== (const expected& lhs, const expected& rhs) noexcept
            {
                if (lhs._has_value != rhs._has_value)
                    return false;
                if (lhs._has_value)
                    return lhs._val == rhs._val;
                return lhs._err == rhs._err;
            }

            friend bool operator!= (const expected& lhs, const expected& rhs) noexcept
            {
                return ! (lhs == rhs);
            }

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

            template <typename F>
            auto and_then (F&& f) const&
            {
                using result_t = decltype (f (std::declval<const T&>()));
                if (_has_value)
                    return f (_val);
                else
                    return result_t (unexpect, _err);
            }

            template <typename F>
            auto map (F&& f) const&
            {
                using U = decltype (f (std::declval<const T&>()));
                if (_has_value)
                    return expected<U, E> (f (_val));
                else
                    return expected<U, E> (unexpect, _err);
            }

            template <typename F>
            auto or_else (F&& f) const&
            {
                using result_t = decltype (f (std::declval<const E&>()));
                if (! _has_value)
                    return f (_err);
                else
                    return result_t (_val);
            }

            // For && overloads
            template <typename F>
            auto map (F&& f) &&
            {
                using U = decltype (f (std::move (_val)));
                if (_has_value)
                    return expected<U, E> (f (std::move (_val)));
                return expected<U, E> (unexpect, std::move (_err));
            }

            template <typename F>
            auto and_then (F&& f) &&
            {
                using result_t = decltype (f (std::move (_val)));
                if (_has_value)
                    return f (std::move (_val));
                return result_t (unexpect, std::move (_err));
            }

            template <typename F>
            auto or_else (F&& f) &&
            {
                using result_t = decltype (f (std::move (_err)));
                if (! _has_value)
                    return f (std::move (_err));
                return result_t (std::move (_val));
            }

            // default constructor
            explicit expected (const T& val) : _has_value (true)
            {
                new (&_val) T (val);
            }

            expected (unexpect_tag_t, const E& err) : _has_value (false)
            {
                new (&_err) E (err);
            }

            // assignment operators
            expected& operator= (const expected& other)
            {
                if (this != &other)
                {
                    expected temp (other); // May throw
                    swap (*this, temp); // Never throws
                }
                return *this;
            }

            expected& operator= (expected&& other) noexcept
            {
                if (this != &other)
                {
                    expected temp (std::move (other)); // Should not throw
                    swap (*this, temp); // Never throws
                }
                return *this;
            }

            // copy constructor
            expected (const expected& other) : _has_value (other._has_value)
            {
                if (_has_value)
                    new (&_val) T (other._val);
                else
                    new (&_err) E (other._err);
            }

            // move constructor
            explicit expected (T&& v) : _has_value (true)
            {
                new (&_val) T (std::move (v));
            }

            expected (unexpect_tag_t, E&& e) : _has_value (false)
            {
                new (&_err) E (std::move (e));
            }

            template <typename... Args>
            explicit expected (CASPI::in_place_t, Args&&... args)
                : _has_value (true)
            {
                new (&_val) T (std::forward<Args> (args)...);
            }

            template <typename... Args>
            explicit expected (unexpect_tag_t, Args&&... args)
                : _has_value (false)
            {
                new (&_err) E (std::forward<Args> (args)...);
            }

            expected (expected&& other) noexcept : _has_value (other._has_value)
            {
                if (_has_value)
                    new (&_val) T (std::move (other._val));
                else
                    new (&_err) E (std::move (other._err));
            }

            ~expected()
            {
                if (_has_value)
                    _val.~T();
                else
                    _err.~E();
            }

            CASPI_NO_DISCARD bool has_value() const
            {
                return _has_value;
            }

            CASPI_NO_DISCARD bool has_error() const
            {
                return ! _has_value;
            }

            const T& value() const
            {
                CASPI_ASSERT (_has_value, "Expected does not hold a value.");
                return _val;
            }

            const E& error() const
            {
                CASPI_ASSERT (! _has_value, "Expected does not hold an error.");
                return _err;
            }
    };

    template <typename T, typename E>
    class noexcept_expected
    {
            CASPI_STATIC_ASSERT (std::is_nothrow_destructible<T>::value, "T must be noexcept destructible");
            CASPI_STATIC_ASSERT (std::is_nothrow_destructible<E>::value, "E must be noexcept destructible");

            CASPI_STATIC_ASSERT (std::is_nothrow_move_constructible<T>::value, "T must be noexcept move constructible");
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