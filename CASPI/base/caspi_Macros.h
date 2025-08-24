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


* @file caspi_Macros.h
* @author CS Islay
* @brief A collection of marcos for useful purposes.
*
************************************************************************/

#ifndef CASPI_MACROS_H
#define CASPI_MACROS_H

#if defined(CASPI_DISABLE_HEAP) && !defined(CASPI_FORCE_ENABLE_HEAP)
#define CASPI_DISABLE_HEAP(Class) \
    void* operator new (std::size_t) = delete; \
    void operator delete (void*) = delete;
#endif

#if defined(CASPI_DEFAULT_NOEXCEPT_DTOR)
    #define CASPI_DEFAULT_NOEXCEPT_DTOR(CLASS) \
    virtual ~CLASS() noexcept = default;
#endif

#endif //CASPI_MACROS_H
