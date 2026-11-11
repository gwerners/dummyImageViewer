#include "Task.h"
#ifndef _WIN32
#include <sys/wait.h>
#include <csignal>
#endif
#include "Core.h"

#ifdef USING_QT
Task::Task(QObject* parent) : QObject(parent) {}
#else
Task::Task() {}
#endif

#ifndef _WIN32
static void func(int) {
  wait(NULL);
}
#endif

int Task::run() {
#ifndef _WIN32
  signal(SIGCHLD, func);
#endif
  Core core;
  core.run();
#ifdef USING_QT
  emit finished();
#endif
  return 0;
}
