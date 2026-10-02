# MPF fuzzing

`mpf-fuzz-smoke` replays and deterministically mutates the checked-in corpus on every CI run. It
exercises all four frontend corpora and both target pipelines with strict resource limits.

Matlab seeds include explicit unary and two-threshold range validator calls, mixed quoted
boundary flags, optional/default/output validation, and scalar-versus-array argument
representation. These complement relational, IEEE threshold and size-conversion seeds.
Nested default calls inside Matlab selectors/slice bounds and Python tuples also exercise dense
semantic-table growth; regression tests force the same arena relocation under ASan/UBSan.
The invocation-context seed covers binding-based bare calls with default inputs, per-call
`nargout`/`nargout()` reads, zero/one/multiple-output demand, and source parameter shadowing.
Mutations run through both independent target ABIs and the frame/query/source verifiers.

Clang/libFuzzer builds are enabled with `-DMPF_BUILD_FUZZERS=ON`. This build mode instruments
the production core, both enabled backends, and the facade with
`-fsanitize=fuzzer-no-link,address,undefined`; only the fuzz driver links libFuzzer's main.
It is separate from source-coverage and release-performance builds. Merely instrumenting
the driver does not provide coverage-guided testing of the compiler.

Prepare the checked-in text seeds beneath root `build/` before running. The driver consumes
two control bytes (source language modulo four; target low bit), followed by the unchanged
source bytes. The preparer makes both JavaScript and cpp variants for every source seed and
refuses output outside `build/`. Do not feed unframed text to the driver or write mutations
into the checked-in source corpus:

```sh
cmake -DSOURCE_DIR="$PWD" -DCORPUS_DIR="$PWD/build/fuzz/framed-corpus" \
  -P tests/fuzz/prepare_corpus.cmake
build/fuzz/tests/mpf-transpiler-fuzzer build/fuzz/framed-corpus \
  -runs=1000 -max_len=4096 -artifact_prefix=build/fuzz/
```

A framed crashing input can be replayed by passing the file to `mpf-transpiler-fuzzer`, or
minimized into `build/fuzz/` with libFuzzer's
`-minimize_crash=1 -exact_artifact_path=<output>` workflow. `mpf-fuzz-smoke` consumes the
original language-directory text format, not framed libFuzzer artifacts. The corpus contract
checks every language/target prefix and payload byte plus source-directory write rejection.
