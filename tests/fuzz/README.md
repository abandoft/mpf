# MPF fuzzing

`mpf-fuzz-smoke` replays and deterministically mutates the checked-in corpus on every CI run. It
exercises all four frontend corpora and both target pipelines with strict resource limits.

Matlab seeds include explicit unary and two-threshold range validator calls, mixed quoted
boundary flags, optional/default/output validation, and scalar-versus-array argument
representation. These complement relational, IEEE threshold and size-conversion seeds.
Nested default calls inside Matlab selectors/slice bounds and Python tuples also exercise dense
semantic-table growth; regression tests force the same arena relocation under ASan/UBSan.

Clang/libFuzzer builds are enabled with `-DMPF_BUILD_FUZZERS=ON`. Copy the checked-in seeds to a
directory under `build/` before running because libFuzzer adds coverage-increasing inputs to the
corpus:

```sh
cmake -E copy_directory tests/fuzz/corpus build/fuzz/corpus
build/fuzz/tests/mpf-transpiler-fuzzer build/fuzz/corpus
```

A crashing input can be replayed by passing its directory to `mpf-fuzz-smoke`, or minimized into
`build/fuzz/` with libFuzzer's `-minimize_crash=1 -exact_artifact_path=<output>` workflow. Fuzz
artifacts must never be written into the checked-in seed directory.
