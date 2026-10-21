#include <string>
#include "version.hpp"
#include <iostream>

std::string Version::getVersion() const {

    int x;
    int* ptr1 = &x;

    delete ptr1;

    std::cout << ptr1 << std::endl; 

    return ENGINE_VERSION;
}