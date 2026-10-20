#include "EffectRackLayout.h"

int calculateEffectDropIndex (int y, std::span<const int> rowHeights, int rowGap)
{
    if (rowHeights.empty()) return -1;
    int top = 0;
    for (size_t row = 0; row < rowHeights.size(); ++row)
    {
        top += rowHeights[row] + rowGap;
        if (y < top) return static_cast<int> (row);
    }
    return static_cast<int> (rowHeights.size()) - 1;
}
