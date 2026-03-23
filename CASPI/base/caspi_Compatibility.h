#ifndef CASPI_COMPATIBILITY_H
#define CASPI_COMPATIBILITY_H

#include <memory>
#include <utility>


#include "caspi_Features.h"



namespace CASPI
{
#if defined(CASPI_FEATURES_HAS_MAKE_UNIQUE)

    using std::make_unique;
#else
    template <typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args)
    {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
#endif // CASPI_FEATURES_HAS_MAKE_UNIQUE
} // namespace CASPI

#endif //CASPI_COMPATIBILITY_H
