# GitHub Actions responsibility matrix

MPF keeps each required check aligned with one failure domain. Workflows that run on
`main`, pull requests, merge queues, and manual dispatch also expose `workflow_call`, so the
release orchestrator invokes the exact same definitions instead of maintaining a weaker copy.

| Workflow file | Public workflow name | Required check | Responsibility |
|---|---|---|---|
| `build-and-test.yml` | Build & Test | `Build & Test / Required` | Fast GCC build, core tests, install layout, and failure diagnostics |
| `platform-compatibility.yml` | Platform Compatibility | `Platform Compatibility / Required` | GCC, Clang, AppleClang, and MSVC matrix; full differential tests and install validation |
| `code-quality.yml` | Code Quality | `Code Quality / Required` | actionlint, clang-format, clang-tidy, and warnings-as-errors |
| `memory-safety.yml` | Memory Safety | `Memory Safety / Required` | ASan/UBSan instrumented compiler paths; weekly scheduled replay |
| `test-coverage.yml` | Test Coverage | `Test Coverage / Required` | Full source-based coverage suite and the 85% production-line gate |
| `performance-regression.yml` | Performance Regression | `Performance Regression / Required` | Versioned latency, throughput, arena, output-size, determinism, and concurrency budgets |
| `security-analysis.yml` | Security Analysis | `Security Analysis / Required` | Capability-aware CodeQL and pull-request dependency review |
| `release.yml` | Release | n/a | Version policy, gate orchestration, provenance, publication, and public-asset verification |
| `release-candidate.yml` | Reusable / Release Candidate | n/a | Internal three-platform test/install/package implementation called only by `release.yml` |

Repository branch protection should require the seven stable `*/ Required` checks above.
The terminal checks intentionally hide matrix expansion and optional capability jobs from the
branch-protection contract while still failing if any required upstream job fails. Workflow
changes are validated by `Code Quality / actionlint`.

## Release dependency graph

```text
annotated MAJOR.MINOR.PATCH tag on main
                  |
        version + changelog policy
                  |
  +---------------+---------------+
  | seven canonical workflows run |
  | in parallel on the tagged SHA  |
  +---------------+---------------+
                  |
  Linux / macOS / Windows candidates
  build -> full tests -> install -> external consumers
        -> ZIP -> checksum -> archive verification
                  |
       build-provenance attestation
                  |
          publish GitHub Release
                  |
  download all six public assets and verify again
```

Packaging cannot start until Build & Test, Platform Compatibility, Code Quality, Memory
Safety, Test Coverage, Performance Regression, and Security Analysis all succeed. Each
candidate then tests the exact source revision used to create its installation tree. The
candidate contract includes all functional, differential, generated-code, fuzz-smoke,
backend-isolation, and installed-package tests; the dedicated performance workflow owns the
non-instrumented performance test to avoid duplicate or noisy measurements.

The installation tree is validated before archiving: the CLI version, public headers, CMake
package, exact-version rejection, frontend/backend external consumers, diagnostics schema,
and byte-identical license must all pass. Archive validation rejects multiple roots, path
traversal, repository/build residue, missing public components, version mismatches, license
changes, and checksum mismatches.

Published releases contain exactly three ZIP files and three SHA-256 files. Checksum sidecars
use a carriage-return-free coreutils format that remains consumable by Unix checksum tools even
when produced on Windows. ZIP files receive GitHub build-provenance attestations. The final job
downloads the public Release rather than reusing runner-local files, checks its draft/prerelease
state and exact asset set, and repeats archive, checksum, version, license, and GitHub attestation
verification.

Every external Action is pinned to a full commit SHA with a version comment. Action updates are
reviewed explicitly and committed through the normal mainline release workflow; no automated
pull-request bot is enabled. CodeQL results from branch and pull-request runs are uploaded to
code scanning.
The tag-gate invocation reruns the same query suite but does not upload a duplicate tag SARIF;
the local analysis must still complete successfully before packaging.

## Trigger and artifact policy

- Pull requests and merge queues run every required workflow. New pushes cancel stale runs for
  the same branch or pull request; tag release runs are never cancelled.
- Main pushes run all seven required workflows. Memory safety, performance, and security also
  have independent weekly schedules.
- `Release` publishes only for an annotated numeric tag without a `v` prefix that is reachable
  from `main`. Manual dispatch performs the complete gate and package dry run but never
  publishes.
- Failure diagnostics are retained for 7–14 days. Coverage reports are retained for 14 days;
  performance reports and verified release candidates are retained for 30 days.
- All generated files, downloaded assets, reports, and nested consumer builds remain below the
  repository root `build/` directory.
