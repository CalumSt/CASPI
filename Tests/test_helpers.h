#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>


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
std::vector<FloatType> range (FloatType start, FloatType end, int numberOfSteps)
{
    std::vector<FloatType> result;
    auto timeStep = (end - start) / numberOfSteps;
    for (int i = 0; i < numberOfSteps; i++)
    {
        auto value = start + static_cast<FloatType> (timeStep * i);
        result.push_back (value);
    }
    return result;
}

inline void saveToFile(const std::string& filename, std::vector<double> x, std::vector<double> y)
{
    const std::filesystem::path path(filename);
    std::filesystem::create_directory(path.parent_path());

    std::cout << path.stem().string() << " saved to: " << absolute(path).string() << "\n";
    std::ofstream file(path.string());
    for (int i = 0; i < x.size(); i++) {
        file << x[i] << "," << y[i] << "\n";
    }
    file.close();
}

// Helper function to compare two vectors
template <typename T>
void compareVectors(const std::vector<T>& expected, const std::vector<T>& actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i], actual[i]) << "Vectors differ at index " << i;
    }
}



#endif //TEST_HELPERS_H
