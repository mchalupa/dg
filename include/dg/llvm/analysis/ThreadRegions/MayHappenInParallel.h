#ifndef MAYHAPPENINPARALLEL_H
#define MAYHAPPENINPARALLEL_H

#include <set>

#include "ThreadRegion.h"

class MayHappenInParallel
{
private:
    std::set<ThreadRegion *> threadRegions_;
public:
    MayHappenInParallel(std::set<ThreadRegion *> threadRegions);

    std::set<ThreadRegion *> parallelRegions(ThreadRegion * threadRegion);
};

#endif // MAYHAPPENINPARALLEL_H
