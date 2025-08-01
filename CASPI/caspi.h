#ifndef CASPI_H
#define CASPI_H

// Base
#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "base/caspi_Platform.h"
#include "base/caspi_Traits.h"

// Core
#include "core/caspi_CircularBuffer.h"
#include "core/caspi_Core.h"
#include "core/caspi_Expected.h"
#include "core/caspi_Phase.h"
// Audio Utils

// Utilities
#include "maths/caspi_FFT.h"
#include "maths/caspi_FFT_new.h"
#include "maths/caspi_Maths.h"

// oscillators
#include "oscillators/caspi_BlepOscillator.h"
#include "oscillators/caspi_PMOperator.h"

// Filters
#include "filters/caspi_LadderFilter.h"
#include "filters/caspi_OnePoleFilter.h"
#include "filters/caspi_SvfFilter.h"

// Gain
#include "gain/caspi_Gain.h"

// Envelopes
#include "envelopes/caspi_Envelope.h"

// Synthesizers
#include "synthesizers/caspi_PMAlgorithm.h"

#endif // CASPI_H