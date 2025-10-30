using Avalonia.Controls;
using System;
using System.Threading.Tasks;

namespace userinterface.Animations;

public interface ICollectionAnimationHelper : IDisposable
{
    bool AreAnimationsActive { get; }

    int GetItemCount();

    Control? GetContainerAtIndex(int index);

    double CalculatePositionForIndex(int index);

    ValueTask AnimateItemToPositionAsync(int itemIndex, double position, int staggerIndex = 0);

    ValueTask AnimateAllItemsAsync(int focusIndex = -1);

    ValueTask ExpandAsync();

    ValueTask CollapseAsync();

    void UpdateAllZIndexes();

    void UpdateInteractionState(bool enabled);

    void CancelAllAnimations();
}
