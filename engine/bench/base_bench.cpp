#include <benchmark/benchmark.h>
#include <vector>
#include <algorithm>


static void BM_StdSort(benchmark::State& state) {
	std::vector<int> data(state.range(0), 42);
	for (auto _ : state) {
		std::vector<int> copy = data;
		std::sort(copy.begin(), copy.end());
	}
}


BENCHMARK(BM_StdSort) -> Range(8,8<<10);

BENCHMARK_MAIN();
