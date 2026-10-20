#pragma once

#include <span>

[[nodiscard]] int calculateEffectDropIndex (int y, std::span<const int> rowHeights, int rowGap);
