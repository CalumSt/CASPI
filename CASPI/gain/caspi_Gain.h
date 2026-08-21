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


* @file gain/caspi_Gain.h
* @author CS Islay
* @brief  Top-level gain module. Pulls in the ramped Gain<F> processor and
*         the Waveshaper<F> saturation node.
* @ingroup gain
*
* Gain<F> is a plain ramped-gain struct (level staging); Waveshaper<F> is
* an AudioNode built on top of it for saturation/distortion. Both are
* level-shaping utilities with no shared state, so they're grouped under
* one module and included together here.
*
************************************************************************/

#ifndef CASPI_GAIN_H
#define CASPI_GAIN_H

#include "gain/caspi_GainProcessor.h"
#include "gain/caspi_Waveshaper.h"

#endif // CASPI_GAIN_H
