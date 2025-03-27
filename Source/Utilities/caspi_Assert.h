#ifndef CASPI_ASSERT_H
#define CASPI_ASSERT_H

#ifndef CASPI_ASSERT
#include <cassert>
// Assert in debug only. This is used for readability, but you may want to define additional assertion behaviour.
#define CASPI_ASSERT(x,msg) assert(x && msg)
#endif

#ifndef CASPI_STATIC_ASSERT
#include <cassert>
// static assert that fails at compile time. Macro is used for readability.
#define CASPI_STATIC_ASSERT(x,msg) static_assert(x && msg)
#endif

#endif //CASPI_ASSERT_H
