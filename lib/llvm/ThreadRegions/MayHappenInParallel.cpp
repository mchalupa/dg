#include "dg/llvm/ThreadRegions/MayHappenInParallel.h"

using namespace std;

MayHappenInParallel::MayHappenInParallel(set<ThreadRegion *> threadRegions)
        : threadRegions_(move(threadRegions)) {}

set<ThreadRegion *>
MayHappenInParallel::parallelRegions(ThreadRegion * /*unused*/) {
    return threadRegions_;
}
