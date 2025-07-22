#ifndef CASPI_H
#define CASPI_H

// Core
#include "base/caspi_Platform.h"
#include "base/caspi_Features.h"
#include "base/caspi_Base.h"
#include "base/caspi_Assert.h"
#include "base/caspi_CircularBuffer.h"
#include "base/caspi_Constants.h"
// Audio Utils

// Utilities
#include "maths/caspi_FFT.h"

// oscillators
#include "oscillators/caspi_BlepOscillator.h"
#include "oscillators/caspi_PMOperator.h"

// Filters
#include "Filters/caspi_SvfFilter.h"
#include "Filters/caspi_LadderFilter.h"
#include "Filters/caspi_OnePoleFilter.h"

// Gain
#include "Gain/caspi_Gain.h"

// Envelopes
#include "Envelopes/caspi_Envelope.h"

// Synthesizers
#include "Synthesizers/caspi_PMAlgorithm.h"

#endif // CASPI_H