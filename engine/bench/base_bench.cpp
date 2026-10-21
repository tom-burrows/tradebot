#include <benchmark/benchmark.h>
#include "core/version.hpp"


static void BM_Version(benchmark::State& state) {
	Version v;
	for (auto _ : state) {
		std::string version = v.getVersion();
		benchmark::DoNotOptimize(version);
	}
}


BENCHMARK(BM_Version);

BENCHMARK_MAIN();
