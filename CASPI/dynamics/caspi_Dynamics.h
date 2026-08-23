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


* @file dynamics/caspi_Dynamics.h
* @author CS Islay
* @brief  Top-level dynamics module. Pulls in DynamicsBase and every
*         concrete dynamics processor built on it.
* @ingroup dynamics
*
* DynamicsBase<Derived, F> is the CRTP base (detector, sidechain input,
* parameter API, default gain computer); Compressor<F> is the first
* concrete topology. Future topologies (Limiter, Gate/Expander, FET/Optical
* compressor variants) belong in this module alongside Compressor and
* should be added to this aggregator as they land.
*
************************************************************************/

#ifndef CASPI_DYNAMICS_H
#define CASPI_DYNAMICS_H

#include "dynamics/caspi_DynamicsBase.h"
#include "dynamics/caspi_Compressor.h"

#endif // CASPI_DYNAMICS_H
