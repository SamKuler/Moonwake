#include "EffectRackLayout.h"

#include <algorithm>

int calculateEffectDropIndex (int y, int rowHeight, int rowGap, int rowCount)
{
    if (rowCount <= 0)
        return -1;

    const auto stride = std::max (1, rowHeight + rowGap);
    return std::clamp (y / stride, 0, rowCount - 1);
}
