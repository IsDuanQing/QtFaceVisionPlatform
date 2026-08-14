#include <cassert>
#include <string>
#include <vector>

#include "inference/TensorRTEngine.h"

int main()
{
    ivp::TensorRTEngine engine;
    assert(!engine.isLoaded());

    const bool loaded = engine.loadFromFile(
        "dummy.engine",
        std::vector<std::int64_t>{1, 3, 1088, 1088});
    assert(!loaded);
    assert(!engine.lastError().empty());

    return 0;
}
