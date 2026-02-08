#include <iostream>
#include <string>

static bool read_line(std::string &out) {
  if (!std::getline(std::cin, out)) {
    return false;
  }
  return true;
}

int main() {
  std::string line;
  if (!read_line(line)) {
    return 1;
  }

  if (line == "HELLO SPU 1") {
    std::cout << "READY SPU 1" << std::endl;
  } else {
    std::cout << "ERROR" << std::endl;
    return 1;
  }

  while (read_line(line)) {
    if (line == "PING") {
      std::cout << "PONG" << std::endl;
      continue;
    }
    if (line == "SHUTDOWN") {
      break;
    }
    std::cout << "ERROR" << std::endl;
  }

  return 0;
}
