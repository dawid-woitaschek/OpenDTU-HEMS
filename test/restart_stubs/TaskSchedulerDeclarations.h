#pragma once
#include <functional>
#include <cassert>
constexpr unsigned TASK_SECOND = 1000;
constexpr int TASK_FOREVER = -1;
class Task {
public:
    Task(unsigned interval, int iterations, std::function<void()> callback)
        : interval(interval), iterations(iterations), callback(callback) {}
    void enable() { enabled = true; }
    void restart() { iteration = 0; enabled = true; }
    bool isFirstIteration() const { return iteration == 1; }
    void tick() { assert(enabled); ++iteration; callback(); }
    unsigned interval;
    int iterations;
private:
    std::function<void()> callback;
    unsigned iteration = 0;
    bool enabled = false;
};
class Scheduler {
public:
    void addTask(Task& task) { scheduled = &task; }
    Task* scheduled = nullptr;
};
