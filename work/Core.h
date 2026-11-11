#pragma once
#include <string>

class CorePrivData {
 public:
  CorePrivData() {};
  bool _debug;
  std::string _root;
};
class Core {
 public:
  Core();
  void configure();
  void run();

 private:
  bool exists(const std::string&);
  std::string readFile(const std::string&);
  CorePrivData _priv;
};
