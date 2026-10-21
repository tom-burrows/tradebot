#include <string>
#include "version.hpp"
#include <iostream>

std::string Version::getVersion() const {
    return ENGINE_VERSION;
}