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


* @file caspi_Oscillator.h
* @author CS Islay
* @class caspi_Oscillator
* @brief A base class for oscillators providing a common API
*
************************************************************************/


#ifndef CASPI_OSCILLATOR_H
#define CASPI_OSCILLATOR_H

template <typename FloatType>
class caspi_Oscillator {
public:
    virtual ~caspi_Oscillator() = default;
    virtual FloatType getNextSample() = 0;
    virtual void setFrequency() = 0;
    virtual void reset() = 0;
};

#endif //CASPI_OSCILLATOR_H
