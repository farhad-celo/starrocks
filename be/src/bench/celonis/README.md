
### Build

You may need to comment out some broken SR benchmarks in ../CMakeLists.txt.
``` 
CMAKE_BUILD_TYPE=Release ./build.sh --be --with-bench
```
### Run

```
./be/build_Release/src/bench/celonis/output/calc_throughput_bench
```