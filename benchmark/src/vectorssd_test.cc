#include "vectorssd.h"

int main() {
  vectorssd::DB myDb;
  const char* fileName = "example.txt";
  const float vector[10] = { 102.2, -02.2, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0 };
  const std::string key = "20";
  std::string value;

  myDb.OpenFile(fileName);
  myDb.PutToFile(key, vector);
  return 0;
}
