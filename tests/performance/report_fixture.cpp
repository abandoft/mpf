#include <iostream>

#include "mpf/version.hpp"

// Deterministic gate-contract data, never used by the real benchmark or release gate.
int main() {
  std::cout
      << "{\"schemaVersion\":3,\"projectVersion\":\"" << MPF_VERSION_STRING
      << R"json(","maxLatencyNanoseconds":318000000,"minThroughputBytesPerSecond":100000,"maxPeakArenaBytes":6000,"maxGeneratedBytes":1000,"matlabMaxLatencyNanoseconds":318000000,"matlabMinThroughputBytesPerSecond":100000,"matlabMaxGeneratedBytes":1000,"scenarios":[{"name":"matlab-output-exit","latencyNanoseconds":318000000,"throughputBytesPerSecond":100000,"peakArenaBytes":6000,"generatedBytes":1000}]})json"
      << '\n';
}
