#include "ScopedFile.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <iostream>

ScopedFile::ScopedFile() {
  // constructor
  std::puts("constructed");

  int fd = ::open("/tmp/foo.txt", O_RDONLY);

  if (fd == -1) {
    // failed err # says why
  }
}

ScopedFile::~ScopedFile() {
  // destructor
  std::puts("destroyed");
}

void ScopedFile::foo(bool early) {
  if (early) return;

  throw std::runtime_error("boom");
}