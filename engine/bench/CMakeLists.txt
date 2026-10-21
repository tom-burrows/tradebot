add_executable(
	base_bench
  base_bench.cpp
)

target_link_libraries(
  base_bench
  PRIVATE
  benchmark::benchmark_main
)
