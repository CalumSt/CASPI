/************************************************************************
.d8888b.                             d8b
d88P  Y88b                            Y8P
888    888
888         8888b.  .d8888b  88888b.  888
888            "88b 88K      888 "88b 888
888    888 .d888888 "Y8888b. 888  888 888
Y88b  d88P 888  888      X88 888 d88P 888
 "Y8888P"  "Y888888  88888P' 88888P"  888
                             888
                             888
                             888


* @file caspi_LeakDetector.h
* @author CS Islay
* @brief A simple reference counting leak detector that can be embedded in other classes.
*        Inspired by JUCE's leak detector, but meant to be used with any plugin framework.
*
************************************************************************/

#ifndef CASPI_LEAKDETECTOR_H
#define CASPI_LEAKDETECTOR_H

#include <atomic>
#include "Utilities/caspi_Assert.h"

namespace CASPI
{

template <class OwnerClass>
class LeakDetector {
public:
    LeakDetector() noexcept { increment(); }
    ~LeakDetector() noexcept
    {
        decrement();
        if (getObjectCount() < 0)
        {
           //*** This is a memory leak. If you've hit this, then you've probably deleted something that you shouldn't have.
           CASPI_ASSERT(false, "Memory leak detected!");

        } else if (getObjectCount() > 0)
        {
            //*** This is a memory leak. If you've hit this, then you've probably NOT deleted something that you SHOULD have.
            CASPI_ASSERT(false, "Memory leak detected!");
        }
    }
    void increment() { ++(numObjects); }
    void decrement() { --(numObjects); }
    int getObjectCount() { return numObjects.load(); }

    std::atomic<int> numObjects;
};

#ifndef NDEBUG
#define CASPI_LEAK_DETECTOR(OwnerClass) \
    friend class CASPI::LeakDetector<OwnerClass>;
#else
#define CASPI_LEAK_DETECTOR(OwnerClass)
#endif
} // namespace CASPI
#endif //CASPI_LEAKDETECTOR_H