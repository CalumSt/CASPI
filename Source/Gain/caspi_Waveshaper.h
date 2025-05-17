//
// Created by calum on 09/03/2025.
//

#ifndef CASPI_WAVESHAPER_H
#define CASPI_WAVESHAPER_H

#include <unordered_map>
#include <functional>
#include <string>
#include "Utilities/caspi_Constants.h"
#include "Utilities/caspi_Maths.h"

namespace CASPI::Gain

{
/**
 * A waveshaper class which process input according to a selected function.
 *  Generally used to add saturation, clipping or distortion.
 *  Uniquely, it allows you to use different waveshaper functions for the positive and negative segments,
 *  and to add register your own custom waveshaper function, through a lambda that mathematically represents
 *  the waveshaper function, OR as a file of values to interpolate, OR as a file of waveshaper functions.
 *  Similarly, it is able to save your custom waveshaper function to a file.
 *  Do not register your custom waveshaper in real time (i.e. in a process or processBlock function),
 *  as it WILL block your audio thread.
 *  Additionally, there are no sanity checks done on the waveshaper function. Be careful,
 *  especially with any functions with divisions or asymptotes.
 */
    template <typename FloatType>
    class Waveshaper
    {
       public:
        enum WaveshaperType
        {
            None,
            SoftClip,
            HardClip
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
        template <typename func>
        void registerWaveshape(func Waveshape, std::string name)
        {
            std::function<FloatType (FloatType)> f = Waveshape;
            functionMap[name] = f;
        }

        template <typename func>
        void registerWaveshape(WaveshaperArg<func> &FuncNamePair)
        {
            registerWaveshape(FuncNamePair.Waveshape, FuncNamePair.name);
        }

        void registerWaveshape(std::string &filename)
        {
            /// TODO: implementation class for file reading and serialisation
        }

        void setAsymmetry (bool isAsymmetricFlag, FloatType newAsymmetryPoint)
        {
            isAsymmetric = isAsymmetricFlag;
            asymmetryPoint = newAsymmetryPoint;
        }

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

       inline void applyWaveshape (FloatType &out)
       {
            out = functionMap[waveshape](out);
       }

       std::string getWaveshapeName()
       {
           return waveshape;
       }

       std::string getNegativeWaveshapeName()
       {
           return negativeWaveshape;
       }

  	   void setClipLimit (FloatType newClipLimit)
       {
           clipLimit = newClipLimit;
       }

       void setGain (FloatType newGain)
       {
           gain = newGain;
       }

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
