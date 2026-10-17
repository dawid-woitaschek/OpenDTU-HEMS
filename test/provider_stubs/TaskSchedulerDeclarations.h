#pragma once
#include <functional>
#include <vector>
constexpr int TASK_FOREVER = -1;
class Task {
public:
    void setCallback(std::function<void()> callback) { tick = callback; }
    void setInterval(int) {}
    void setIterations(int) {}
    void enable() {}
    std::function<void()> tick;
};
class Scheduler {
public:
    void addTask(Task& task) { tasks.push_back(&task); }
    void execute() { for (auto* task : tasks) { task->tick(); } }
    std::vector<Task*> tasks;
};
