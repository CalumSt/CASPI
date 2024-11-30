//
// Created by calum on 29/11/2024.
//

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <string>
#include <vector>
#include <matplot/matplot.h>


using namespace matplot;
template <typename FloatType>
void createPlot(std::vector<FloatType> x, std::vector<FloatType> y,const std::string& titleString, std::string filename) {

    // Plot a red dashed line from given x and y data.
    plot(x, y,"r");;
    // Add graph title
    title(titleString);
    show();
    // save figure

    filename = "./Figures/" + filename + ".jpg";
    std::ofstream file("plotlog.txt"); // create a file stream
    std::cout.rdbuf(file.rdbuf()); // set the buffer to the file stream

    save(filename);
}

template <typename FloatType>
std::vector<FloatType> range(FloatType start, FloatType end, FloatType step)
{
    std::vector<FloatType> result;
    for (FloatType i = start; i < end; i += step) {
        result.push_back(i);
    }
    return result;

}

template <typename FloatType>
std::vector<FloatType> range(FloatType start, FloatType end, int numberOfSteps)
{
    std::vector<FloatType> result;
    auto timeStep = (end - start) / numberOfSteps;
    for (int i = 0; i < numberOfSteps; i++) {
        auto value = start + static_cast<FloatType>(timeStep * i);
        result.push_back(value);
    }
    return result;
}



#endif //TEST_HELPERS_H
