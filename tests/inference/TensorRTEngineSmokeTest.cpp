#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "inference/TensorRTEngine.h"

namespace
{

std::size_t volumeOf(const std::vector<std::int64_t>& shape)
{
    std::size_t volume = 1;
    for (std::int64_t dimension : shape)
    {
        if (dimension <= 0)
        {
            return 0;
        }
        volume *= static_cast<std::size_t>(dimension);
    }
    return volume;
}

std::string shapeToString(const std::vector<std::int64_t>& shape)
{
    std::string text = "[";
    for (std::size_t i = 0; i < shape.size(); ++i)
    {
        if (i > 0)
        {
            text += ", ";
        }
        text += std::to_string(shape[i]);
    }
    text += "]";
    return text;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string enginePath = argc > 1
        ? argv[1]
        : "models/yolo11l/defect.engine";
    const std::vector<std::int64_t> inputShape = {1, 3, 1088, 1088};

    ivp::TensorRTEngine engine;
    std::cerr << "step=load begin\n";
    if (!engine.loadFromFile(enginePath, inputShape))
    {
        std::cerr << "load failed: " << engine.lastError() << "\n";
        return 1;
    }
    std::cerr << "step=load ok\n";

    std::vector<float> input(volumeOf(inputShape), 0.0F);
    std::vector<float> output;
    std::vector<std::int64_t> outputShape;
    std::cerr << "step=infer begin\n";
    if (!engine.infer(input, &output, &outputShape))
    {
        std::cerr << "infer failed: " << engine.lastError() << "\n";
        return 2;
    }
    std::cerr << "step=infer ok\n";

    const auto minmax = std::minmax_element(output.begin(), output.end());
    std::cout << "input: " << engine.inputInfo().name << " "
              << shapeToString(engine.inputInfo().shape) << "\n";
    std::cout << "output: " << engine.outputInfo().name << " "
              << shapeToString(outputShape) << "\n";
    std::cout << "output floats: " << output.size() << "\n";
    if (minmax.first != output.end())
    {
        std::cout << "output range: " << *minmax.first
                  << " .. " << *minmax.second << "\n";
    }

    return 0;
}
