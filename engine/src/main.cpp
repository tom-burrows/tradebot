#include "core/version.hpp"
#include <iostream>

using namespace std;

int main() {
    Version v;

    cout << "Version: " + v.getVersion() << std::endl;
}