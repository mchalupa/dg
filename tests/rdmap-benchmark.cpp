#include <vector>
#include <string>

#include "dg/analysis/ReachingDefinitions/RDMap.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "../tools/TimeMeasure.h"

// create two random rd maps of the
// size 'size' and merge them
void run(int size, int times = 100000)
{
    using namespace dg::analysis::rd;

    std::vector<RWNode> rdnodes(size, RWNode());

    while (--times > 0) {
        RDMap A, B;

        // fill in the maps randomly
        for (int i = 0; i < size; ++i) {
            const DefSite& ds = DefSite(&rdnodes[rand() % size], rand(), rand());
            A.add(ds, &rdnodes[rand() % size]);
            for (int j = 0; j < size; ++j) {
                A.update(ds, &rdnodes[rand() % size]);
            }
        }

        for (int i = 0; i < size; ++i) {
            const DefSite& ds = DefSite(&rdnodes[rand() % size], rand(), rand());
            B.add(ds, &rdnodes[rand() % size]);
            for (int j = 0; j < size; ++j) {
                B.update(ds, &rdnodes[rand() % size]);
            }
        }

        // merge them
        A.merge(&B);
    }

}

void test(int size)
{
    dg::debug::TimeMeasure tm;
    std::string msg = "[200000 iter] Sets of size max ";
    msg += std::to_string(size);
    msg += " -- ";

    tm.start();
    run(size, 200000);
    tm.stop();
    tm.report(msg.c_str());
}

int main()
{
    test(1);
    test(3);
    test(5);
    test(10);
    test(15);
    test(20);
    test(30);
    test(50);
    test(100);
    test(200);
    test(500);
}
