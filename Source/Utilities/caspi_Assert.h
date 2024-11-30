#ifndef CASPI_ASSERT_H
#define CASPI_ASSERT_H

#ifndef CASPI_ASSERT
#include <cassert>
#define CASPI_ASSERT(x,msg) assert(x && msg)
#endif

#ifndef CASPI_STATIC_ASSERT
#include <cassert>
#define CASPI_STATIC_ASSERT(x,msg) static_assert(x && msg)
#endif

#endif //CASPI_ASSERT_H
