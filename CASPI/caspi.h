#ifndef CASPI_H
#define CASPI_H

// Base
#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "base/caspi_Platform.h"
#include "base/caspi_Compatibility.h"
#include "base/caspi_Traits.h"
#include "base/caspi_RealtimeContext.h"
#include "base/caspi_SIMD.h"

// Core
#include "core/caspi_Span.h"
#include "core/caspi_AudioBuffer.h"
#include "core/caspi_Core.h"
#include "core/caspi_Expected.h"
#include "core/caspi_Phase.h"
#include "core/caspi_Parameter.h"

// External dependencies
#include "external/caspi_External.h"

// Utilities
#include "maths/caspi_FFT.h"
#include "maths/caspi_Maths.h"

// oscillators
#include "oscillators/caspi_BlepOscillator.h"
#include "oscillators/caspi_Operator.h"
#include "oscillators/caspi_WaveTableOscillator.h"
#include "oscillators/caspi_LFO.h"
#include "oscillators/caspi_Noise.h"

// Filters
#include "filters/caspi_OnePoleFilter.h"
#include "filters/caspi_SvfFilter.h"

// Gain
#include "gain/caspi_Gain.h"

// Envelopes
#include "controls/caspi_Envelope.h"
#include "controls/caspi_ModMatrix.h"

// Synthesizers
#include "synthesizers/caspi_FMGraph.h"

#endif // CASPI_H