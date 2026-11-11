#ifdef USING_QT
#include <QCoreApplication>
#include <QtCore>
#endif
#include "Task.h"

int main(int argc, char** argv) {
#ifdef USING_QT
  QCoreApplication a(argc, argv);
  Task* task = new Task(&a);
  QObject::connect(task, SIGNAL(finished()), &a, SLOT(quit()));
  QTimer::singleShot(0, task, SLOT(run()));
  return a.exec();
#else
  Task task;
  return task.run();
#endif
}
