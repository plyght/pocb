#pragma once

#include <functional>

namespace mac {

void installNewLittleWindowHotkey(std::function<void()> handler);

}  // namespace mac
