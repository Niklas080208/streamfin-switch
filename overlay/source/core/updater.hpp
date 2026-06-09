#pragma once

#include <cstddef>

// Opt-in "check for updates" against the GitHub releases API. Runs on a
// background thread; and UI polls Poll() for the result.
namespace updater {

    enum class State { Idle, Checking, UpToDate, Available, Failed };

    void Init();
    void Exit();

    void  Check();                                  // kick off a check (no-op while one is in flight)
    State Poll(char *latest, size_t latest_sz);     // current state; fills the latest tag when known

}
