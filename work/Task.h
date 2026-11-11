#pragma once
#ifdef USING_QT
#include <QObject>
#endif

#ifdef USING_QT
class Task : public QObject {
  Q_OBJECT
 public:
  Task(QObject* parent = 0);
 public slots:
  int run();
 signals:
  void finished();
};
#else
class Task {
 public:
  Task();
  int run();
};
#endif
