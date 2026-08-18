/**
 * @file caspi_Waveshaper.h
 * @brief Wavetable-based waveshaping with configurable transfer functions.
 * @ingroup gain
 *
 * Applies saturation, clipping or distortion by mapping input samples
 * through configurable waveshaper functions. Supports asymmetric shaping
 * with separate positive and negative transfer curves.
 */

#ifndef CASPI_WAVESHAPER_H
#define CASPI_WAVESHAPER_H

#include <unordered_map>
#include <functional>
#include <string>
#include "base/caspi_Constants.h"
#include "maths/caspi_Maths.h"

namespace CASPI::Gain

{
/**
 * @brief Wavetable-based waveshaper for saturation and distortion.
 *
 * Applies a transfer function to input samples. Supports different
 * functions for positive and negative signal segments, plus custom
 * user-registered functions via lambdas or file import.
 * Do not register custom waveshapes in the audio thread (they may block).
 */
    template <typename FloatType>
    class Waveshaper
    {
       public:
        /** @brief Built-in waveshaper transfer function types. */
        enum WaveshaperType
        {
            None,       /**< Passthrough (no shaping). */
            SoftClip,   /**< Soft clipping. */
            HardClip    /**< Hard clipping. */
        };

        template <typename func>
        struct WaveshaperArg
        {
            func Waveshape;
            std::string name;
        };

        template <typename func>
        Waveshaper(std::initializer_list<WaveshaperArg<func>> args)
        {
            for (auto arg : args)
                {
                    registerWaveshape(arg);
                }

        }

        Waveshaper(const std::string &waveshapeFile)
        {
            registerWaveshape(waveshapeFile);
        }

        /**
        * https://johannesugb.github.io/cpu-programming/how-to-pass-lambda-functions-in-C++/
        * E.G. registerWaveshape([](FloatType x) { return x * x; }, "Square");
        */
        /**
         * @brief Register a custom waveshaper function by name.
         *
         * @tparam func Callable type (e.g. lambda).
         * @param Waveshape Callable accepting FloatType and returning FloatType.
         * @param name Name to identify this waveshape (used for selection).
         */
        template <typename func>
        void registerWaveshape(func Waveshape, std::string name)
        {
            std::function<FloatType (FloatType)> f = Waveshape;
            functionMap[name] = f;
        }

        /**
         * @brief Register a custom waveshaper from a WaveshaperArg pair.
         *
         * @tparam func Callable type.
         * @param FuncNamePair Struct containing callable and name string.
         */
        template <typename func>
        void registerWaveshape(WaveshaperArg<func> &FuncNamePair)
        {
            registerWaveshape(FuncNamePair.Waveshape, FuncNamePair.name);
        }

        /**
         * @brief Register a waveshaper from a file.
         * @note Not yet implemented.
         */
        void registerWaveshape(std::string &filename)
        {
        }

        /**
         * @brief Enable asymmetric waveshaping with a crossover point.
         *
         * @param isAsymmetricFlag  True to enable asymmetric processing.
         * @param newAsymmetryPoint Threshold below which the negative waveshape is used.
         */
        void setAsymmetry (bool isAsymmetricFlag, FloatType newAsymmetryPoint)
        {
            isAsymmetric = isAsymmetricFlag;
            asymmetryPoint = newAsymmetryPoint;
        }

       /**
        * @brief Process a single sample through the waveshaper.
        *
        * @param input Input sample.
        * @return Processed (shaped) sample, restricted to [-1, 1].
        */
       FloatType render(FloatType input)
       {
            auto output = input;

            applyWaveshape(output);

            if (isAsymmetric)
            {
                if (input < asymmetryPoint)
                {
                    applyWaveshape(negativeWaveshape, output);
                }
                else
                {
                    applyWaveshape(waveshape, output);
                }
            } else
            {
                applyWaveshape(waveshape, output);
            }

            return restrict(output);
       }

       /**
        * @brief Apply the currently selected waveshape to a sample in place.
        * @param out Sample to modify in place.
        */
       inline void applyWaveshape (FloatType &out)
       {
            out = functionMap[waveshape](out);
       }

       /** @brief Get the name of the active waveshape function. */
       std::string getWaveshapeName()
       {
           return waveshape;
       }

       /** @brief Get the name of the negative-segment waveshape function. */
       std::string getNegativeWaveshapeName()
       {
           return negativeWaveshape;
       }

       /** @brief Set the clip limit for hard/soft clipping modes. */
  	   void setClipLimit (FloatType newClipLimit)
       {
           clipLimit = newClipLimit;
       }

       /** @brief Set the pre-waveshaper gain (linear). */
       void setGain (FloatType newGain)
       {
           gain = newGain;
       }

       /** @brief Set the pre-waveshaper gain in dBFS. */
       void setGainDBFS (FloatType newGainDBFS)
       {
           gain = CASPI::Maths::dBFSToLinear(newGainDBFS);
       }

       private:

           FloatType restrict(FloatType x)
           {
               auto out = x;
               if (x > CASPI::Constants::one<FloatType>) { out = CASPI::Constants::one<FloatType>; }
               if (x < -CASPI::Constants::one<FloatType>) { out = -CASPI::Constants::one<FloatType>; }

               return out;

           }

           FloatType hardClip (FloatType x)
           {
               auto out = 0.5 * (abs(x + clipLimit) - abs(x - clipLimit));
               return out;
           }

           FloatType softClip (FloatType x)
           {
               auto out = (x > clipLimit) ? clipLimit : x;
               out = (x < -clipLimit) ? -clipLimit : x;
               return out;
           }

           FloatType analog (FloatType x)
           {
               auto out = x;
               restrict(out);
               if (x == 0) { out = 0; }
               else if (x > 0) { out =  1 / pow(x, analogAmount); }
               else { out =  -1 / pow(-x, analogAmount); }
               return out;
           }

           FloatType arraya (FloatType x)
           {
                return 3/2 * (x) * (1 - x * x / 3);
           }

        	FloatType sigmoid (FloatType x)
           {
                return (2 / (1 + exp(-gain * x))) - 1;
           }

           FloatType hyperbolicTangent (FloatType x)
           {
               return (tanh(gain * x) / tanh(x));
           }

           FloatType arctangent (FloatType x)
           {
               return (atan(gain * x) / atan(x));
           }

           std::string waveshape = "Linear";
           std::string negativeWaveshape = "Linear";

           bool isAsymmetric        = false;
           FloatType asymmetryPoint = CASPI::Constants::zero<FloatType>;
           FloatType clipLimit      = CASPI::Constants::one<FloatType>;
           FloatType gain           = CASPI::Constants::zero<FloatType>;
           FloatType analogAmount   = CASPI::Constants::zero<FloatType>;

           /// use strings to allow the user to register their own waveshapes
           /// an enum would be preferable, but this is more flexible
           /// maybe there's something more flexible to achieve this in terms of templates?
           std::unordered_map<std::string, std::function<FloatType (FloatType)>> functionMap =
           {
                {"Linear",   [](FloatType x) { return x;}},
                {"SoftClip", [this](FloatType x) { return softClip(x); }},
                {"HardClip", [this](FloatType x) { return hardClip(x); }},
                {"Sine",     [](FloatType x) { return std::sin(x); }},
                {"Tan",      [](FloatType x) { return std::tan(x); }},
                {"Arctan",   [](FloatType x) { return std::atan(x); }},
                {"Cubic",    [](FloatType x) { return x * x * x; } }

            };
    };
}
#endif //CASPI_WAVESHAPER_H
