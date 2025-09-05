#include "../../CASPI/maths/caspi_Maths.h"
#include <gtest/gtest.h>

TEST (MathsTest,MidiToHz_test)
{
    const std::vector<int> noteNums = {69, 25, 128};
    const std::vector<double>  results = {440.0,34.648,13289.75};
    for (int i = 0; i < noteNums.size(); i++)
    {
        auto result = CASPI::Maths::midiNoteToHz<double> (noteNums.at(i));
        ASSERT_NEAR (result, results.at(i),1e-3);
    }

}
