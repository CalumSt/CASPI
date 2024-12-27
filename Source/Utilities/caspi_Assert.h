#ifndef CASPI_ASSERT_H
#define CASPI_ASSERT_H

#ifndef CASPI_ASSERT
#include <cassert>
#ifdef NDEBUG
#define CASPI_ASSERT(x,msg) // empty macro
#else
#define CASPI_ASSERT(x,msg) assert(x && msg) // Assert in debug only
#endif
#endif

#ifndef CASPI_STATIC_ASSERT
#include <cassert>
#define CASPI_STATIC_ASSERT(x,msg) static_assert(x && msg)
#endif

#endif //CASPI_ASSERT_H
