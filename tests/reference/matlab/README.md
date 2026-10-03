# Native MATLAB reference execution

This directory runs original MATLAB source on **R2024b**. It is not a frontend grammar
fixture directory: the observation runner deliberately uses MATLAB constructs that MPF
has not yet implemented, including function handles and conditionally missing outputs.

The manual/reusable `Matlab Reference` workflow has two distinct results:

1. `invocation_context.m` is executed by MATLAB and compared with the actual generated
   JavaScript and strictly compiled C++ results from the differential runner. Its current
   receiver coverage includes single brackets, mixed/repeated/all ignored slots, nested
   actual evaluation, scalar and parameter-dependent tuple results, rejected assigned
   outputs despite discards, and retaining the caller's `ans`.
2. Twenty-seven native observations record conditional output assignment, missing requested
   outputs, ignored receiver positions/count, validation/error ordering, defaults, shared
   input/output bindings, scoped `ans`, and forbidden direct invocation queries in argument
   defaults/input validators/output validators. They are ground truth for pending implementation,
   **not evidence that MPF already supports those cases**.

The first actual R2024b run rejected a direct `nargout` default that MPF had incorrectly
accepted. Legal execution fixtures now evaluate defaults through a separate helper; the compiler has a binding-aware
context check, and deliberately invalid definitions live in separate files loaded through
caught observations. This preserves the rejection evidence without making the observation
collector itself unparseable. A called helper's body is a separate workspace, not a direct
query in the caller's `arguments` block.

The workflow uses the full-SHA-pinned MathWorks
[Setup MATLAB](https://github.com/matlab-actions/setup-matlab) and
[Run MATLAB Command](https://github.com/matlab-actions/run-command) actions. MathWorks
documents automatic batch licensing for public projects using the command action; no
MATLAB Coder, MATLAB Compiler, Engine API, private license token, or toolbox is required here.
The workflow explicitly checks public visibility and requests R2024b rather than `latest`.

The original script is copied byte-for-byte to `official/source/invocation_context.m` and
executed there with its original basename. MATLAB's `run` changes the working directory;
executing among unrelated example files had produced a `transpose.m` builtin-name conflict.
The isolated directory removes that unintended name resolution. No warning is filtered:
unexpected text still causes transcript equality to fail.

All evidence remains under root `build/matlab-reference/`: the original source snapshot,
raw MATLAB transcript, version/revision provenance, structured native observations, actual
target outputs, and generated JavaScript/C++ code. The verifier checks full source SHA,
R2024b, snapshot bytes, case identity, complete observation inventory/types, executed target
paths, and normalized output equality. It rejects empty/skipped targets and changed evidence.
`output-semantics-contract.json` records the actual values, classes, sizes, exception IDs,
first-error message fragments, and builtin availability from R2024b Update 10 run
[37080926914](https://github.com/abandoft/mpf/actions/runs/37080926914). That run successfully
executed MATLAB and collected all 27 cases; its final parity check rejected the unrelated
directory warning. The frozen native contract is a regression baseline, not MPF parity.

| Native observation | R2024b result |
|---|---|
| Conditional second output | Unassigned and unrequested: succeeds; requested and assigned: returns the value |
| Requested missing first/second, including `~` | `MATLAB:unassignedOutputs`; an ignored receiver still requests its position |
| `[first,~,third]` | Callee count is 3; selected values are `[3,23]` |
| Assigned, unrequested or discarded invalid output | Validation still fails |
| Missing first output plus invalid second output | Second-output validation fails before missing-output checking |
| Parenthesized or bare zero-demand call | Updates `ans` if the first output exists; void/absent output retains it |
| Failed output validation | Retains the caller's previous `ans` |
| Direct `nargout` in argument defaults/input/output validators | `MATLAB:functionValidation:RestrictedExternal` |
| `isargout` builtin | Absent (`exist(...,'builtin') == 0`); do not import Octave semantics |

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
