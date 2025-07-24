#ifndef CASPI_H
#define CASPI_H

// Core
#include "core/caspi_Platform.h"
#include "core/caspi_Features.h"
#include "core/caspi_Base.h"
#include "core/caspi_Assert.h"
#include "core/caspi_CircularBuffer.h"
#include "core/caspi_Constants.h"
// Audio Utils

// Utilities
#include "maths/caspi_FFT.h"
#include "maths/caspi_FFT_new.h"
#include "maths/caspi_Maths.h"

// oscillators
#include "oscillators/caspi_BlepOscillator.h"
#include "oscillators/caspi_PMOperator.h"

// Filters
#include "filters/caspi_SvfFilter.h"
#include "filters/caspi_LadderFilter.h"
#include "filters/caspi_OnePoleFilter.h"

// Gain
#include "gain/caspi_Gain.h"

// Envelopes
#include "envelopes/caspi_Envelope.h"

// Synthesizers
#include "synthesizers/caspi_PMAlgorithm.h"

#endif // CASPI_H