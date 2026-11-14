// The BPS navigator connection a headless BB10 process must keep: without it the navigator
// kills the process 6 s after launch (phase0.md, gate 0.3). Start it before anything else.
#pragma once
#include <functional>

namespace bbm {

class Navigator {
public:
    // Runs the BPS thread (init, request events, loop); on_exit is called from it on
    // NAVIGATOR_EXIT. If BPS is not there (or on the host) it logs "no navigator" and returns.
    void start(std::function<void()> on_exit);
};

}  // namespace bbm
