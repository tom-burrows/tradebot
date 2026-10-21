# Useful Commands

A list of useful commands I've written down to remember as I get up to speed with C++ again.

## CMake

### Configuring CMake

This command generates the build system, and installs packages via vcpkg

```
cmake -S engine -B engine/build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

### Building With CMake

cmake --build engine/build

### Testing with CMake

ctest --test-dir engine/build --output-on-failure