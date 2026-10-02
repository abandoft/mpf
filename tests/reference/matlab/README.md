# Native MATLAB reference execution

This directory runs original MATLAB source on **R2024b**. It is not a frontend grammar
fixture directory: the observation runner deliberately uses MATLAB constructs that MPF
has not yet implemented, including function handles and ignored output receivers.

The manual/reusable `Matlab Reference` workflow has two distinct results:

1. `invocation_context.m` is executed by MATLAB and compared with the actual generated
   JavaScript and strictly compiled C++ results from the differential runner.
2. Twenty-three native observations record conditional output assignment, missing requested
   outputs, ignored receiver positions/count, validation/error ordering, defaults, shared
   input/output bindings, and scoped `ans`. They are ground truth for pending implementation,
   **not evidence that MPF already supports those cases**.

The workflow uses the full-SHA-pinned MathWorks
[Setup MATLAB](https://github.com/matlab-actions/setup-matlab) and
[Run MATLAB Command](https://github.com/matlab-actions/run-command) actions. MathWorks
documents automatic batch licensing for public projects using the command action; no
MATLAB Coder, MATLAB Compiler, Engine API, private license token, or toolbox is required here.
The workflow explicitly checks public visibility and requests R2024b rather than `latest`.

All evidence remains under root `build/matlab-reference/`: the original source snapshot,
raw MATLAB transcript, version/revision provenance, structured native observations, actual
target outputs, and generated JavaScript/C++ code. The verifier checks full source SHA,
R2024b, snapshot bytes, case identity, complete observation inventory/types, executed target
paths, and normalized output equality. It rejects empty/skipped targets and changed evidence.

~~~sh
gh workflow run matlab-reference.yml --repo abandoft/mpf --ref main
~~~

Inspect the exact run's **MATLAB execution step and artifact**, not only a verifier's
message or matching JSON. The ordinary CTest verifier-contract test uses clearly labeled
synthetic positive/negative fixtures to test rejection logic; it does not run MATLAB and
must never count as native acceptance.

This workflow is initially an explicit capability/diagnostic run, not one of the seven
release-required checks. The 0.8.0 output-semantic acceptance remains open until the pending
cases have actual MPF/native parity and the release workflow consumes that acceptance.
