#ifndef CASPI_FEATURES_H
#define CASPI_FEATURES_H
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


* @file caspi_Features.h
* @author CS Islay
* @brief A collection of macros to define features based on platform.
*
************************************************************************/

#include "caspi_Platform.h"

/**
* The following features are defined based on the platform:
*    CASPI_FEATURES_HAS_CONCEPTS
*/

#if defined(CASPI_CPP_VERSION) &&(CASPI_CPP_VERSION >= 202002L) && defined(__cpp_concepts) && (__cpp_concepts >= 201907L)
    #define CASPI_FEATURES_HAS_CONCEPTS
#endif



#endif //CASPI_FEATURES_H
