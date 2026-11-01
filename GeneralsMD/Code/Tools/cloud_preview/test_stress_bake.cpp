// No renderer or device: exercise every supported slice count and work bound.
#include "CloudStressBake.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <vector>

int main()
{
    for (unsigned slices = 6; slices <= 96; slices += 6) {
        for (unsigned budget = 1; budget <= 100; ++budget) {
            CloudStress::BakeSlices progress;
            progress.begin(slices, budget);
            std::vector<bool> seen(slices, false);
            unsigned renders = 0, written = 0;
            while (!progress.complete()) {
                unsigned thisFrame = 0;
                while (!progress.complete() && thisFrame < budget) {
                    const unsigned slice = progress.next();
                    assert(slice < slices && !seen[slice]);
                    seen[slice] = true;
                    ++thisFrame; ++written;
                }
                assert(thisFrame <= budget);
                assert(progress.remaining() == slices - written);
                ++renders;
            }
            assert(written == slices && renders == (slices + budget - 1) / budget);
            assert(progress.next() == slices);
            progress.begin(slices, budget);
            progress.next();
            progress.cancel();
            assert(progress.complete() && progress.remaining() == 0);
            progress.begin(slices, budget);
            assert(progress.next() == 0); // A new generation discards partial progress.
        }
    }
    CloudStress::BakeSlices progress;
    progress.begin(24, 8);
    for (unsigned i = 0; i < 8; ++i) assert(progress.next() == i * 3);
    for (unsigned i = 0; i < 8; ++i) assert(progress.next() == i * 3 + 1);
    for (unsigned i = 0; i < 8; ++i) assert(progress.next() == i * 3 + 2);
    assert(progress.complete());
    std::puts("cloud stress bake traversal checks passed");
}
