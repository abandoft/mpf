## 0.6.3

- Matlab sparse matrices now support indexed assignment through one linear selector or two row/column selectors.
- Sparse assignment accepts scalar, colon, slice, ordered or repeated numeric, logical, and empty selectors within the delivered static real rank-two contract.
- Scalar replacement expands across the selected elements, while dense and sparse nonscalar replacements follow Matlab's nonsingleton-dimension shape rules.
- Repeated target indices are applied in Matlab column-major order, with the last replacement value winning deterministically.
- Assigning an exact zero removes the corresponding CSC entry instead of storing an explicit zero.
- Linear and two-subscript assignments can grow a sparse matrix while preserving Matlab's column-major shape rules.
- Null assignment supports vector linear deletion and the legal single-axis row or column deletion forms.
- Sparse right-hand sides and self-referential sparse selections are accepted without materializing a dense copy of the source matrix.
- Sparse updates are validated completely before a transactional commit, so invalid selectors or replacement values do not partially modify the target.
- Generated JavaScript and C++17 independently sort, collapse, and merge updates into canonical CSC storage in O(nnz + k log k).
- A typed `SparseMutationPlan` now carries assignment or deletion identity, shape, storage, scalar-expansion, duplicate-write, and zero-write policies through every compiler layer.
- Cross-layer verifiers reject corrupted sparse-mutation identity, shape, storage, policy, or inactive-state facts before emission.
- Added an executable sparse-assignment example covering insertion, overwrite, zero removal, repeated indices, growth, deletion, sparse replacement, and self-aliasing on both targets.

## 0.6.2

- Matlab sparse matrices now support read-only scalar access through one linear index or two row/column subscripts.
- Sparse selection accepts full colon, positive or negative-step slices, ordered or repeated numeric selectors, logical selectors, and empty selectors.
- Linear sparse results follow Matlab shape rules: numeric matrix selectors retain their shape, logical matrices and full colon produce columns, and vector/vector indexing retains the source orientation.
- Two-subscript selection forms the Matlab Cartesian product with a `numel(rows) × numel(columns)` result.
- Nonscalar sparse indexing preserves canonical CSC storage and never materializes a dense copy of the source matrix.
- Full-colon selection remaps CSC entries directly in O(nnz), while submatrix selection scans chosen columns through an ordered row map.
- Generated JavaScript and C++17 use independent checked sparse-index runtimes; neither target depends on artifacts from the other backend.
- A typed `SparseIndexPlan` now carries scalar/selection identity, input and result shapes, and source/result storage through semantic analysis, MIR, JavaScript LIR, and `cpp` LIR.
- Cross-layer verifiers reject corrupted sparse-index identity, arity, shape, type, storage, or inactive-state facts before emission.
- `full` now accepts rank-two dense or CSC selections with dynamic or zero result extents when their storage representation is known.
- Sparse assignment, more than two selectors, complex or sparse selectors, N-dimensional linear results, and dynamic, empty, or complex sparse sources continue to fail closed with stable diagnostics.
- Added an executable sparse-indexing example with dual-target behavior, source-map preservation, out-of-bounds rejection, and fuzz regression coverage; production line coverage is 91.24% (33,780/37,023).

## 0.6.1

- Matlab now accepts every R2024 `sparse` call form within MPF's nonempty, statically shaped real rank-two contract: `sparse(A)`, `sparse(m,n)`, inferred triplets, explicitly sized triplets, and the `nzmax` form.
- `sparse(m,n)` and explicitly sized empty triplets now create zero-valued canonical CSC matrices without materializing dense storage.
- Triplet construction supports scalar expansion for row indices, column indices, or values while requiring all nonscalar inputs to have equal element counts.
- Inferred triplet construction derives its output dimensions from positive compile-time literal indices; explicitly sized construction validates known and runtime indices against the requested shape.
- Duplicate triplet coordinates are accumulated deterministically in column-major order, and exact cancellation removes the stored entry.
- The `nzmax` argument is validated as a nonnegative compile-time integer and becomes a capacity hint in generated C++17 without changing observable matrix contents.
- Generated JavaScript and C++17 build canonical sorted CSC data directly from triplets instead of allocating an intermediate dense matrix.
- Ordinary and conjugating transpose now preserve sparse CSC storage for the delivered real sparse contract and use target-owned transpose helpers.
- A typed `SparseConstructionPlan` now carries constructor identity, result shape, triplet cardinalities, and reserve intent through semantic analysis, MIR, JavaScript LIR, and `cpp` LIR.
- Independent HIR, MIR, JavaScript LIR, and `cpp` LIR verifiers reject corrupted sparse arity, shape, cardinality, reserve, storage, or inactive-state facts before emission.
- Sparse constructor and transpose source locations remain mapped through both target-specific runtime calls; neither output backend depends on the other's artifacts.
- Runtime checks now reject fractional or nonpositive triplet indices, mismatched nonscalar triplets, out-of-range coordinates, invalid dimensions, and nonfinite duplicate accumulation with stable diagnostics.
- Added executable, differential, generated-runtime rejection, fuzz, architecture, and performance coverage for zero, empty, inferred, sized, reserved, scalar-expanded, duplicate, and transposed sparse matrices.

## 0.6.0

- Matlab `sparse(A)` now converts nonempty, statically shaped real rank-two arrays into canonical compressed sparse column storage.
- Matlab `full`, `issparse`, and `nnz` now support the delivered dense/CSC conversion, storage-query, and nonzero-count workflows.
- Sparse real square left division now supports vectors and multiple right-hand-side columns in generated JavaScript and C++17.
- Tridiagonal sparse coefficients use a compact O(nnz) factorization, while other square patterns use partial-row-pivoted sparse LU without creating a dense coefficient copy.
- Sparse square right division reuses the transposed sparse solve and preserves sparse results according to Matlab left/right operand rules.
- Exact singular and nearly singular sparse systems now emit the same stable condition warnings as the existing dense square solvers.
- Generated JavaScript uses a private tagged CSC value, while generated C++17 uses a typed `mpf_runtime::sparse_matrix`; the two runtimes remain independent.
- Runtime validation rejects malformed CSC pointers, unordered row indices, zero or nonfinite stored entries, and incompatible solve shapes.
- Generated C++17 sparse validation remains warning-clean under strict GCC dangling-reference analysis without suppressing compiler diagnostics.
- Unsupported sparse constructors, indexing, transpose, reshape, logical, element-wise, multiplication, power, rectangular, complex, and zero-extent cases fail before emission with `MPF2054`.
- Numeric type planning now preserves Matlab binary64 arrays while keeping Python operand-returning short-circuit and conditional-expression result types correct in generated C++17.
- Added executable sparse solve and condition-warning examples with source-map checks, fuzz coverage, and dual-target differential validation.

## 0.5.9

- Matlab complex rectangular left division now supports overdetermined and underdetermined systems in generated JavaScript and C++17.
- Complex rectangular right division follows Matlab's conjugate-transpose solve identity for both solve shapes.
- Multiple right-hand-side columns share one complex column-pivoted QR factorization.
- Rank-revealing complex Householder QR uses deterministic column pivoting and a working-precision rank tolerance.
- Rank-deficient complex systems return a pivoted basic least-squares solution and emit a stable warning instead of failing silently.
- Matrix plans now carry an explicit rank-revealing column-pivoted-QR factorization policy through semantic analysis, MIR, and both target LIRs.
- Semantic, MIR, JavaScript LIR, and `cpp` LIR debug schemas advance to v13, v19, and v25 with factorization-policy corruption rejection.
- JavaScript and C++17 retain independent complex rectangular runtimes with finite-value, rectangularity, shape, and solve-plan validation.
- Source maps preserve complex overdetermined, underdetermined, left-division, and right-division operator locations.
- Added executable complex rectangular and rank-deficient Matlab examples with dual-target differential and warning validation.
- Added complex rectangular fuzz coverage and a dedicated compilation-performance workload.

## 0.5.8

- Matlab complex rank-two matrices now support true matrix multiplication in generated JavaScript and C++17.
- Dense complex square left division now supports vector or multiple-column right-hand sides.
- Complex square right division follows the conjugate-transpose solve identity instead of reusing real transpose semantics.
- Exact Hermitian positive-definite coefficient matrices now use a dedicated complex Cholesky factorization and solve path.
- Non-Hermitian and non-positive-definite complex square systems fall back to magnitude-pivoted dense LU deterministically.
- Complex square solvers now estimate reciprocal condition and preserve the existing singular and nearly-singular warning behavior.
- Complex square matrix power supports zero, positive, and negative ECMAScript-safe integer exponents through exponentiation by squaring.
- Generated runtimes reject malformed, nonfinite, shape-inconsistent, or fractional complex matrix operations at their owned boundaries.
- Matrix plans now carry an explicit real or complex numeric domain and a distinct complex-square structure policy through every compiler layer.
- Semantic, MIR, JavaScript LIR, and `cpp` LIR debug schemas advance to v12, v18, and v24 with numeric-domain validation and corruption rejection.
- JavaScript and C++17 use independent complex-matrix runtimes; neither target reads generated code or runtime artifacts from the other.
- Complex-matrix runtime fragments are dependency-checked and omitted entirely from real-only matrix programs.
- Source maps retain the original locations of complex multiplication, left/right division, and positive or negative matrix powers.
- Added executable examples for Hermitian, dense pivoted, singular, and nearly-singular complex matrix behavior with dual-target differential validation.
- Added a complex matrix fuzz seed and generated-runtime rejection coverage for dynamically supplied invalid power exponents.

## 0.5.7

- Matlab now accepts imaginary numeric literals with trailing `i` or `j`; the predefined imaginary-unit names remain ordinary, shadowable identifiers.
- Added Matlab scalar `complex`, `conj`, `real`, `imag`, and complex-aware `abs` operations.
- Complex scalar arithmetic supports addition, subtraction, multiplication, stable scaled division, unary signs, and integer powers including the zero exponent.
- Compatible-size complex array arithmetic supports scalar expansion and N-dimensional element-wise operations in both output targets.
- Matlab ordinary transpose `.'` and conjugate transpose `'` now preserve their distinct behavior for real and complex vectors and rank-two arrays.
- Complex values survive indexed assignment and `reshape` without losing their numeric identity or column-major array semantics.
- Local Matlab functions can pass and return runtime-selected real or complex scalars and arrays without requiring a JavaScript generation pass.
- Generated JavaScript uses an independent, checked complex-number runtime and omits complex helpers entirely for real-only programs.
- Generated C++17 uses an independent `std::complex`-based runtime and infers dynamic numeric result types without depending on JavaScript artifacts.
- Unsupported complex comparisons, logical operations, reductions, and matrix solve/multiply/power paths now fail before emission with stable `MPF2053` diagnostics.
- Numeric class and real/complex identity are validated throughout semantic analysis, MIR, and each target-specific lowering pipeline, including corruption rejection tests.
- Source maps now retain source locations for complex construction, arithmetic, array mutation, reshape, and transpose operations.
- Added an executable Matlab complex example, dual-target differential coverage, generated-code assertions, and a dedicated fuzz regression seed.
- Python scalar return annotations now guide generated C++17 types, preventing annotated integer functions from being emitted with incompatible inferred types.
- Fortran optional writable arguments and Python truthiness regressions found by the expanded full suite are fixed in both output backends.

## 0.5.6

- Matlab now preserves the distinct precedence and evaluation behavior of `~`, `&`, `|`, `&&`, and `||` instead of treating them as interchangeable Boolean operators.
- Element-wise logical operations support scalar expansion and compatible-size N-dimensional numeric or logical arrays in generated JavaScript and C++17.
- Scalar short-circuit expressions evaluate operands lazily and at most once in both output targets.
- Matlab `if` and `while` conditions now follow nonempty, all-elements-nonzero array truth semantics; ambiguous non-scalar condition compositions fail closed.
- Matlab `all` and `any` now support the default first nonsingleton dimension, constant `dim`, constant `vecdim`, and the `'all'` option.
- Logical reductions preserve result rank and extents for vectors, matrices, and N-dimensional arrays, including dimensions above the input rank.
- Empty reductions preserve exact `0×N` and `N×0` shapes and use the Matlab identities `all=true` and `any=false` where applicable.
- Local functions with runtime-known rank can use `all(values,'all')` and `any(values,'all')` without requiring a statically known container shape.
- Generated JavaScript uses a checked column-major reduction kernel and retains zero extents through the existing non-enumerable shape descriptor.
- Generated C++17 uses an independent typed recursive-`std::vector` reduction kernel and remains warning-clean under strict C++17 compilation.
- Logical evaluation and reduction contracts are independently verified through semantic analysis, MIR, JavaScript LIR, and `cpp` LIR before emission.
- Scalar division now carries an explicit zero-denominator policy through HIR, MIR, and both target LIRs; generated C++17 uses portable target-runtime calls for Matlab/TypeScript IEEE results, while Python true and floor division report stable errors in both targets.
- Source maps cover logical and reduction runtime calls; character arrays, dynamic dimensions, duplicate or invalid dimensions, and unsupported unknown-rank reductions produce stable diagnostics.
- Added executable dual-target examples, differential cases, cross-layer corruption checks, runtime assertions, and dedicated Matlab fuzz seeds.

## 0.5.5

- Matlab square left and right division now detects full real tridiagonal coefficient matrices, matching the structure introduced for dense inputs in Matlab R2024a.
- Tridiagonal systems now use an adjacent-row partial-pivoting LU factorization with compact diagonal storage instead of general dense LU.
- The tridiagonal runtime supports multiple right-hand sides and dedicated transpose solves, so right division uses the same specialized contract.
- Exact symmetric positive-definite matrices now use Cholesky factorization and triangular substitution in generated JavaScript and C++17.
- Symmetric matrices that are not positive definite fall back deterministically to partial-pivoting dense LU without exposing a failed Cholesky result.
- Square structure selection now follows diagonal, triangular, tridiagonal, symmetric-positive-definite, and dense fallback priority in both output targets.
- Tridiagonal and Cholesky kernels now participate in the same iterative 1-norm reciprocal-condition estimation used for square-system warnings.
- Exact singular tridiagonal systems and nearly singular positive-definite systems emit the stable Matlab-style warning and continue with the computed result.
- The verified matrix structure policy now represents full real-square classification through semantic analysis, MIR, JavaScript LIR, and `cpp` LIR.
- Generated helper names now describe the real structured-square contract and no longer retain the earlier diagonal/triangular-only helper identity.
- Added executable left/right tridiagonal, positive-definite, symmetric-indefinite, singular, and nearly singular examples with dual-target output and warning validation.
- Expanded source-map assertions, verifier corruption checks, architecture checks, and Matlab fuzz seeds for the advanced structure paths.

## 0.5.4

- Matlab square left and right division now detects diagonal, upper-triangular, lower-triangular, and general dense real coefficient matrices at runtime.
- Diagonal systems now use direct element-wise solves instead of allocating and factoring a dense LU matrix.
- Upper- and lower-triangular systems now use dedicated forward or backward substitution in generated JavaScript and C++17.
- General square systems continue to use partial-pivoting dense LU as the deterministic fallback.
- Matlab right division classifies the transposed divisor and uses the same structure-aware solver contract as left division.
- Reciprocal-condition estimation now follows the selected diagonal, triangular, or dense kernel, including transpose solves required by the estimator.
- Exact singular and nearly singular structured systems emit the existing stable Matlab-style warnings and continue with the computed IEEE result.
- Generated C++17 now recursively widens mixed integer and floating-point matrix rows to one concrete numeric container type.
- Matrix structure policy is verified through semantic analysis, MIR, JavaScript LIR, and `cpp` LIR before code emission.
- Source maps now retain the original locations of structured left and right division expressions.
- Added executable structured-solve and condition-warning examples, dual-target differential cases, verifier corruption tests, and a Matlab fuzz regression seed.

## 0.5.3

- Matlab rectangular left and right division now use rank-aware column-pivoted QR for both overdetermined and underdetermined systems.
- Underdetermined solves now return a pivoted basic solution instead of the previously incorrect minimum-norm result.
- Rank-deficient rectangular systems continue with a stable working-precision warning and a basic least-squares result.
- Exact singular square left and right division now warn and continue, preserving the tested Matlab-style finite and IEEE-infinity components instead of terminating generated programs.
- Nearly singular square systems now estimate reciprocal condition with the existing LU factors, emit a distinct `RCOND` warning, and continue with the computed result.
- Matrix conditioning behavior is represented by a verified `MatrixConditionPolicy` across semantic analysis, MIR, JavaScript LIR, and `cpp` LIR.
- Generated JavaScript and C++17 now own independent partial-pivoted LU, transpose-solve, condition-estimation, and column-pivoted QR runtimes.
- Invalid non-vector linear deletion remains rejected consistently, while singular-square execution is no longer misclassified as an unsupported runtime operation.
- Source maps now retain both left-division and right-division locations for condition-aware square solves.
- Generated C++ now rejects impossible zero-extent coordinate conversion before division, keeping empty-array output warning-clean under MSVC `/WX`.
- Release SHA-256 sidecars now use a carriage-return-free format that standard Unix checksum tools can verify even when the package is built on Windows.
- Added executable exact-singular and nearly-singular Matlab examples with two-target output and warning-count validation.
- Expanded cross-layer corruption checks, generated-code assertions, architecture checks, and the Matlab fuzz corpus for conditioned solves.

## 0.5.2

- Matlab `[]` now has its canonical `0×0` double-array shape instead of being treated as a rank-one empty list.
- `reshape` now accepts zero extents and preserves shapes such as `0×5`, including dimensions that cannot be recovered from nested container structure alone.
- Transpose preserves exact zero-extent shapes, so `0×N` and `N×0` arrays remain distinct in generated JavaScript and C++17.
- Scalar arithmetic and relational comparisons with empty arrays now retain the complete broadcast result shape and element type.
- Empty and colon-based section reads preserve their statically known result rank and zero extents.
- Linear assignment to `[]` follows Matlab row-vector growth semantics, while growth from shaped empty matrices retains the planned dimensions and column-major layout.
- Generated JavaScript carries exact array shape in a checked, non-enumerable descriptor, preserving normal array iteration and serialization behavior.
- Generated C++17 now consumes target-owned static shape plans for rank, `length`, transpose, broadcasting, and growth without depending on JavaScript output.
- Invalid or contradictory empty-array shape facts are rejected consistently before target emission and again at generated-runtime boundaries.
- Added an executable two-target empty-array example, source-map checks, cross-layer corruption tests, a fuzz regression seed, and a dedicated performance-release scenario.

## 0.5.1

- Matlab indexed assignment now grows row vectors, column vectors, matrices, and N-dimensional arrays through scalar, colon-range, and ordered numeric selectors.
- Linear growth preserves Matlab column-major order and vector orientation, extends the final matrix/tensor dimension when required, and initializes gaps with the element type's default value.
- Matlab `[]` assignment now deletes vector elements or one selected dimension of a matrix/tensor, including scalar, slice, numeric, and logical selectors with duplicate positions removed once.
- Shape-changing writes support statically known bounds, runtime indexes, local-function parameters, and dynamic `end` expressions in independently generated JavaScript and C++17.
- Analyzer-owned `IndexedMutationContract` records overwrite, resize, grow, and erase behavior together with linear layout, deletion axis, shape source, and input/result shapes.
- Semantic, MIR, and target LIR schemas advance to v7, v12, and v18; each layer verifies mutation rank, direction, axis, and shape consistency before emission.
- MIR now treats growth and deletion as whole-storage writes; memory-dependence analysis forms required hazards and then prunes covered same-root history, avoiding stale partial regions and quadratic frontier growth.
- Generated JavaScript uses checked nested-array resizing and axis deletion, while generated C++17 uses typed nested-`std::vector` templates without consuming JavaScript output.
- Both runtimes validate safe sizes, selector bounds and types, rectangular rank, replacement cardinality, and ambiguous multi-dimensional deletion at their owned boundary.
- Source maps now cover generated growth and deletion calls, and invalid linear matrix deletion, multi-axis deletion, and out-of-bounds deletion fail closed with stable diagnostics.
- Added static and runtime-shaped executable differential coverage, N-dimensional generated-code checks, cross-layer corruption tests, and a dedicated Matlab fuzz seed.

## 0.5.0

- Matlab compatible-size arithmetic and relational comparisons now support operands whose rank and extents are available only when a local function is instantiated or executed.
- Runtime broadcast dispatch preserves scalar results, scalar expansion, row-vector normalization, missing trailing singleton dimensions, and general rectangular nested arrays.
- HIR, MIR, JavaScript LIR, and `cpp` LIR now carry a verified `static_extents` or `runtime_operands` broadcast shape source; unknown rank is explicit rather than encoded as a static empty shape.
- Generated JavaScript derives and validates rectangular operand shapes once before its column-major flatten/stride kernel; incompatible extents fail with a stable runtime error.
- Generated C++17 combines template-known rank with runtime extents, returns the correct scalar or nested `std::vector` type, and rejects ragged or incompatible operands without depending on JavaScript output.
- C++ logical-array `sum` now returns a numeric count instead of collapsing all nonzero counts to `true`.
- Matlab `end` now works when an array extent is known only at runtime, including local-function parameters and computed selectors.
- Runtime-sized `end` supports linear column-major indexing, per-dimension indexing, colon bounds, arithmetic such as `end - 1`, and numeric selector arrays such as `[1 end]`.
- Dynamic `end` reads and writes now execute independently in generated JavaScript and C++17 for scalar elements and N-dimensional sections.
- Static extents retain their constant-folded fast path, so existing fixed-shape indexing does not pay for runtime extent resolution.
- HIR, MIR, JavaScript LIR, and `cpp` LIR now carry verified runtime-axis or runtime-linear extent plans instead of asking emitters to infer source semantics.
- Semantic, MIR, and target LIR debug schemas advance to v6, v11, and v17 with explicit broadcast-shape sources and per-selector extent identities.
- Generated JavaScript resolves selector closures against the active axis length or column-major element count while evaluating the indexed container only once.
- Generated C++17 uses typed selector callables and a generalized column-major element accessor, including a C++17-safe type-probe path without lambdas in unevaluated operands.
- Added cross-layer corruption tests, source-map assertions, generated-code checks, and a two-target executable differential example for dynamic `end` reads and writes.
- Added a dedicated dynamic-`end` fuzz seed and compilation-performance scenario; read-only memory frontiers with no compatible function write are now pruned, retaining the existing release thresholds.
- Added executable scalar/array dynamic-broadcast differential coverage, cross-layer corruption rejection, source-map checks, a fuzz seed, and a dedicated compilation-performance scenario.

## 0.4.9

- Matlab matrix left division now solves static full-rank dense real square, overdetermined, and underdetermined systems with one or more right-hand-side columns.
- Matlab matrix right division now supports row vectors and rank-two left operands with static full-rank dense real square or rectangular divisors.
- Matlab matrix power now supports zero, positive, and negative ECMAScript-safe integer exponents for static square dense real matrices.
- Square solves use partial pivoting; rectangular least-squares and minimum-norm solves use column-pivoted Householder QR in both generated JavaScript and C++17.
- Matlab array division by a scalar and scalar left division of an array now preserve matrix-operator semantics without requiring element-wise spelling.
- Matlab indexing now supports ordered numeric selector arrays, repeated indices, empty selectors, and logical selectors in either linear or per-dimension positions.
- Logical selectors with runtime-known extents now validate their shape in generated code instead of requiring every mask size to be known during compilation.
- Matrix operation kind, solve class, and input/output shapes now remain explicit and verified across HIR, MIR, JavaScript LIR, and `cpp` LIR.
- Semantic, MIR, and target LIR schemas advance to v4, v9, and v15; every scalar, slice, numeric, logical, or empty selector now has a verified per-subscript identity.
- Rank-deficient systems, non-square powers, matrix exponents, non-finite solve values, and unsafe or fractional exponents fail closed deterministically.
- Added executable square and rectangular solve examples, two-target differential coverage, and generated-runtime rejection coverage for rank deficiency.
- Expanded Matlab fuzz, source-map, generated-code, runtime-rejection, and compilation-performance coverage for matrix solve/power and generalized indexing.

## 0.4.8

- Matlab array arithmetic now supports compatible-size implicit expansion across statically known N-dimensional shapes, including singleton and missing trailing dimensions.
- Matlab relational operators now produce Boolean arrays and support the same scalar, exact-shape, and compatible-size expansion rules, enabling expressions such as `A(A >= 30)`.
- JavaScript array expansion uses a flatten-once, precomputed-stride kernel, while scalar and exact-shape operations retain direct fast paths.
- Generated C++17 now provides target-independent typed implementations for N-dimensional expansion and array comparisons, matching JavaScript results.
- Matlab conjugating transpose `'` and non-conjugating transpose `.'` are parsed independently and support current real vectors and rank-two arrays.
- Matlab `end` now resolves from the active indexing dimension or linear element count when extents are statically known, including arithmetic and colon expressions.
- Matlab logical masks now support column-major linear reads and scalar or vector writes, with strict mask-size and replacement-shape validation.
- Unsupported dynamic extents, higher-rank transpose, incompatible masks, growth through `end`, matrix division, and matrix power now fail before code generation with dedicated diagnostics.
- Added five executable Matlab examples and differential cases covering implicit expansion, transpose, `end`, logical indexing, and broadcast comparisons on both output targets.
- Expanded Matlab fuzz regression inputs and cross-layer validation for broadcast plans, transpose identity, logical selection, and optimized intermediate representations.

## 0.4.7

- Matlab now distinguishes matrix operators from element-wise operators such as `.*`, `./`, `.\`, and `.^`, preserving their semantics during parsing.
- Added two-dimensional numeric matrix multiplication with executable JavaScript and C++17 implementations.
- Added basic `+`, `-`, `.*`, `./`, `.\`, and `.^` operations for same-shape arrays and scalar expansion.
- Incompatible shapes and unsupported matrix division or matrix power now produce `MPF2046` instead of unreliable target code.
- Matlab operator identity is represented by strongly typed facts across the AST, HIR, MIR, and target LIR, providing a foundation for broadcasting, transpose, and numeric-type support.
- JavaScript and C++ backend sources now reside in separate target directories, with redundant target prefixes removed from filenames to clarify extension boundaries.
- Added Matlab array-operation differential examples, positive and negative semantic tests, target-code checks, and fuzz regression inputs.
- Updated the Matlab-to-JavaScript support matrix, diagnostics documentation, and project roadmap to reflect current capabilities and known limitations.
- C++ output now assigns distinct target identifiers to lexically shadowed symbols, preventing warning-as-error failures with MSVC while preserving source scope semantics.

## 0.4.6

- CMake packages now require exact version matching; consumers must locate the current version with `find_package(mpf 0.4.6 EXACT ...)`.
- Canonical source-language names are now `matlab`, `python`, `fortran`, and `typescript`, while output targets are `javascript` and `cpp`.
- Removed aliases such as `py`, `f90`, `ts`, `js`, and `c++`; automatic detection by file extension is unchanged.
- C++ API language-name and version parsing now returns `std::optional` for invalid input instead of silently selecting a default language or target.
- Frontend and backend extension interfaces were upgraded to API v6, and legacy name-alias fields were removed.
- The CMake package now exposes only standard component states and exported targets, removing uppercase backend status variables from earlier development snapshots.
- Text and JSON diagnostics now always include complete source ranges for accurate editor highlighting.
- Installed examples, embedding examples, the extension guide, and versioning documentation were updated for the current interface.

## 0.4.5

- Improved memory-dependence analysis across branches, loops, and function calls for more reliable ordering of array and section writes.
- Added the ability to distinguish disjoint N-dimensional regions, reducing conservative restrictions on safe accesses.
- Compilation reports now include memory-dependence statistics to help diagnose complex control flow and performance issues.
- Optimized large control-flow graphs and dependency deduplication, reducing a related benchmark from approximately 354 ms to 43 ms.
- Expanded regression and fuzz validation for loop-carried dependencies, unknown external calls, and indexed writes.

## 0.4.4

- Improved tracking of reads and writes from variables, array elements, sections, argument copies, and writeback operations.
- Propagated actual argument access regions across function calls, improving correctness checks for writable arguments and array sections.
- Improved alias analysis to distinguish definitely disjoint, definitely identical, and indeterminate memory regions.
- Fixed a case where memory-access information could become inconsistent with generated code after optimization.
- Expanded stability tests for indexing, slicing, writeback, and cross-function array access.

## 0.4.3

- Added a unified N-dimensional array-region model supporting contiguous ranges, strided sections, negative strides, and multidimensional rectangular regions.
- Fortran now allows multiple provably disjoint sections of the same array to be passed as writable procedure arguments.
- Actual overlap and indeterminate boundaries continue to produce safety diagnostics.
- Improved handling of empty sections, large strides, and column-major linear selection.
- Added a Fortran disjoint-region example with result validation across gfortran, JavaScript, and C++.

## 0.4.2

- Added a shared optimization pipeline before the JavaScript and C++ backends so both targets consume the same optimized representation.
- Added safe integer and Boolean constant folding while preserving semantics outside JavaScript's exact integer range.
- Normalized static array shapes and removed invalid instructions and simple redundant control flow.
- Improved block-parameter copy propagation while preserving general branch joins and dynamic-value semantics.
- Compilation reports now include pre- and post-optimization sizes, constant-folding counts, and cleanup statistics.
- Added a Python optimization example with consistent results across the source program, JavaScript output, and C++ output.

## 0.4.1

- TypeScript `let` and `const` now support nested block scopes, name shadowing, and assignment to outer variables.
- Added standard C-style TypeScript `for` loops with initialization, conditions, updates, `break`, and `continue`.
- Fixed inaccurate local-variable scope and lifetime in generated JavaScript and C++.
- TypeScript `number` now follows ECMAScript numeric semantics, and array indexing rejects values that cannot be represented safely as integers.
- JavaScript output now includes only the runtime helpers required by the current program.
- Added multi-target execution validation for TypeScript block scopes and `for` loops.

## 0.4.0

- Added a TypeScript frontend with automatic recognition of `.ts`, `.mts`, and `.cts` files.
- Initial support includes `let`/`const`, scalars and typed arrays, assignment, functions, default parameters, conditionals, `while`, loop control, and `console.log`.
- Type annotations in the supported subset can be erased when generating JavaScript or C++17.
- TypeScript `export function` declarations can generate corresponding JavaScript ESM exports.
- Added explicit diagnostics for `var`, loose equality, arrow functions, template strings, and unmodeled object semantics to prevent inconsistent generated behavior.
- Added four-way validation using Node.js source execution, generated JavaScript, generated C++, and declared results.

## 0.3.9

- Improved short-circuit evaluation for conditional expressions, Python `and`/`or`, and comparison chains so operands execute only when required.
- Variable reads, initialization, assignment, indexed writes, and argument writeback now use a unified intermediate representation, reducing behavioral differences between backends.
- Fixed side-effect tracking for writable section arguments during copy-in, copy-out, and writeback.
- Strengthened validation of function parameters, return shapes, dynamic extents, and lazy branches.
- Improved intermediate-representation query and verification performance for large programs.

## 0.3.8

- Refactored compiler intermediate representations to reduce traversal costs for large expression trees and control flow.
- JavaScript and C++ backends now generate code from unified value, type, shape, and storage information.
- Improved detection of corrupted or incomplete intermediate states before errors can propagate to code generation.
- This release does not expand language syntax; it focuses on compilation stability and backend consistency.

## 0.3.7

- Python, Matlab, and Fortran parsers now construct language-specific ASTs directly, reducing duplicate copies and peak memory usage.
- Improved error recovery for all three languages so failed parses do not publish incomplete syntax nodes.
- Language ASTs are now independent, establishing clear boundaries for future implementation of complete official grammars.
- Removed unused shared syntax entry points, reducing coupling when adding new languages.

## 0.3.6

- Added Python `is`, `is not`, `in`, and `not in` operators.
- Improved equality semantics for Boolean, numeric, string, list, and tuple values.
- Added membership operations for strings, lists, and tuples, including mixed comparison chains.
- JavaScript output now distinguishes lists from tuples and preserves reference identity within the supported subset.
- C++ output now supports recursive container comparison and membership; identity comparisons that cannot be represented reliably are explicitly rejected.
- Added Python comparison validation across CPython, JavaScript, and C++.

## 0.3.5

- Improved cross-function checking of types, shapes, tuple return values, and reference parameters.
- Added more accurate handling of read-only borrows, writable borrows, copy-out, copy-in/out, optional forwarding, and omitted arguments.
- Fixed array-section writeback, optional-argument forwarding, and potential aliasing issues in function calls.
- JavaScript and C++ backends now plan function ABIs, temporaries, declaration order, and module layout independently; generating either target no longer depends on the other.
- Improved analysis performance and diagnostics for large function graphs and complex call scenarios.
- Strengthened resource-limit checks so argument expansion cannot bypass node-count limits.

## 0.3.4

- Extended static array-object semantics to general N-dimensional declarations, nested shapes, `RESHAPE`, indexing, and section reads and writes.
- Added a three-dimensional Fortran tensor example with consistent gfortran, JavaScript, and C++ results.
- Added source-language version selection to the public API and CLI; unsupported version-specific features now produce explicit diagnostics.
- Added source-map output, dependency manifests, resource limits, and machine-readable compilation reports.
- Reorganized frontends, shared analysis, and target backends into a clearly layered compilation pipeline while keeping the JavaScript and C++ backends independent.
- Split GitHub automation by validation, portability, quality, sanitizer, coverage, performance, security, and release responsibilities.
- Added fuzzing, performance baselines, installed-package consumer tests, and external extension examples.

## 0.3.3

- Frontends and backends now use discoverable descriptor and registry architectures, simplifying the addition of new source languages and output targets.
- The public API can now enumerate built source languages and target backends.
- Built-in function bindings are declared explicitly by each language and target, and missing target implementations are diagnosed before code generation.
- Metadata remains queryable for disabled backends without compiling or linking their implementations.
- Added extension guides for frontends, backends, and built-in function bindings.

## 0.3.2

- Added chained comparisons and right-associative conditional expressions to Python.
- Comparison chains preserve left-to-right order, single evaluation, and short-circuit behavior.
- Conditional expressions preserve lazy branches and Python truthiness in JavaScript and C++ output.
- Comparisons or branch results that C++ cannot represent statically now produce explicit diagnostics.
- Added four-way result validation across CPython, JavaScript, C++, and declared results.

## 0.3.1

- Added Fortran `SELECT CASE`, `CASE DEFAULT`, and `END SELECT`.
- Added integer, character, and logical selectors with single values, closed intervals, and intervals with omitted bounds.
- Added validation for interval types, reversed intervals, and overlapping cases.
- Character cases now compare according to Fortran blank-padding rules.
- Improved definite-assignment and termination-flow analysis after multi-branch selections.

## 0.3.0

- Added unified C++ code-formatting and static-analysis configuration.
- Added source-coverage reporting with an 85% production line-coverage gate.
- Added formatting, static analysis, coverage, CodeQL, and dependency-security checks to GitHub automation.
- Fixed duplicate branches and invalid state updates discovered by quality checks.
- All quality reports continue to be generated under the `build/` directory.

## 0.2.9

- Python unpacking assignment now supports nested tuple/list patterns and starred targets.
- Starred targets can appear in any position and support empty captures, nested captures, and repeated-name overwrites.
- The right-hand side is evaluated once, with assignment order preserved in JavaScript and C++ output.
- Dynamic lengths, structural mismatches, and heterogeneous starred results that C++ cannot represent now produce explicit diagnostics.
- Added four-way unpacking validation across CPython, JavaScript, C++, and declared results.

## 0.2.8

- Renamed the canonical C++ output target to `cpp`.
- The public API now uses `TargetLanguage::cpp`, and the CLI uses `--target cpp`.
- CMake backend switches, component names, and exported targets now also use `cpp`.
- Generated code still targets C++17; the standard version is no longer part of the target identity.
- This is a 0.x naming change, and the previous name is not retained.

## 0.2.7

- Added flat tuple/list unpacking assignment to Python.
- Added bare `a, b`, parenthesized and bracketed forms, and trailing commas on single targets.
- Added tuple-returning functions, swap assignment, heterogeneous tuples, and repeated targets.
- JavaScript and C++ output both guarantee that the right-hand side is evaluated only once.
- Dynamic unknown lengths and target-count mismatches now produce explicit diagnostics.

## 0.2.6

- Python functions now support scalar default arguments.
- Added positional-only `/`, keyword-only `*`, keyword arguments, and trailing commas.
- Call analysis now supplies defaults and checks duplicate, unknown, missing, and incorrectly positioned arguments.
- Default values are currently limited to side-effect-free immutable scalars; containers, calls, and identifier defaults are explicitly rejected.
- JavaScript and C++ output use the same argument-association result.

## 0.2.5

- Extended Fortran optional arguments to supported scalar and array `INTENT(IN/OUT/INOUT)` forms.
- Added ordered writeback for optional writable actual arguments, array elements, whole arrays, and one- or two-dimensional sections.
- Forwarding optional arguments across procedures now preserves absence or reference identity.
- Fixed omitted optional `OUT` arguments being treated incorrectly as uninitialized reads.
- Added corresponding optional-reference runtime support to JavaScript and C++ output.

## 0.2.4

- Added keyword arguments and the `OPTIONAL` declaration attribute to Fortran.
- Added scalar `OPTIONAL, INTENT(IN)`, `PRESENT`, and optional-argument forwarding.
- Calls now reject unknown keywords, duplicate associations, missing required arguments, and positional arguments after keywords.
- JavaScript uses `undefined` for omitted arguments, while C++ uses typed optional values.
- Unsupported optional arrays and writable optional arguments are explicitly rejected.

## 0.2.3

- Extended writable Fortran procedure arguments to array elements and one- or two-dimensional sections.
- Contiguous and non-contiguous sections preserve writeback semantics through copy-in/copy-out.
- Fortran function return values and section writeback now execute in the correct order.
- Multiple potentially overlapping writable arguments produce conservative diagnostics to prevent unsafe code generation.
- Added section-argument validation across gfortran, JavaScript, and C++.

## 0.2.2

- Added one- and two-dimensional assumed-shape dummy arrays to Fortran.
- Calls now validate scalar/array categories, rank, static extents, and element types.
- C++ output uses shared array storage by reference, while JavaScript output updates array `OUT` arguments in place.
- Non-dummy assumed-shape declarations and unsafe non-contiguous writable sections are explicitly rejected.

## 0.2.1

- Added Fortran `INTENT(OUT)` and `INTENT(INOUT)`.
- `OUT` and `INOUT` actual arguments are passed by reference and update caller variables after the call.
- Call analysis permits uninitialized `OUT` arguments, requires initialized `INOUT` arguments, and checks all return paths.
- Binding the same variable to multiple potentially writable parameters is rejected.
- JavaScript and C++ backends preserve semantics with reference boxes and C++ references, respectively.

## 0.2.0

- Added Fortran functions, subroutines, `RESULT`, `RETURN`, `RECURSIVE`, and `CALL`.
- Added internal and external procedures, dummy parameters, early returns, and named `END` validation.
- Added recursive scalar functions and recursive subroutines.
- Distinguished function expressions from `CALL` subroutine contexts and added diagnostics for misuse.
- Until full reference-argument support became available, dummy-argument modifications that might be passed incorrectly by value were safely rejected.

## 0.1.9

- Improved function-dependency analysis with support for forward calls, direct recursion, and mutual recursion.
- Function return types, element types, and shapes can now propagate along call chains.
- C++ output emits functions in dependency order and creates forward declarations for statically describable recursive functions.
- Recursive functions without reliable C++ return types are explicitly rejected for C++, while JavaScript output remains independently available.
- Improved forwarding of Matlab multiple-output values across functions.

## 0.1.8

- Added Matlab multiple-output assignment with `[a, b] = f(...)`.
- Matlab multiple-output functions now select the first output in single-value contexts according to language rules.
- JavaScript uses arrays and destructuring, while C++ uses `std::tuple` and `std::get`.
- Both backends guarantee single evaluation of the right-hand side in multi-target assignment.
- Added checks for output counts, repeated targets, and invalid right-hand sides.

## 0.1.7

- Replaced the Fortran frontend with a tokenized statement lexer and recursive-descent parser.
- Added the current subset of programs, declarations, `IF`, `DO`, loop control, `PRINT`/`WRITE`, `CALL`, and assignment.
- Free- and fixed-form source both use the new parsing pipeline.
- Fixed valid entity names being misclassified as keywords.
- Fixed standard `WRITE(*,*) value` parsing while retaining the common comma-separated form.

## 0.1.6

- Replaced the Matlab frontend with a tokenized statement lexer and recursive-descent parser.
- Added the current subset of functions, multiple outputs, conditionals, loops, display, assignment, indexed assignment, and expression statements.
- Character vectors, conjugate transpose, and non-conjugate transpose are now distinguished correctly.
- Improved Matlab function return-type propagation and C++ forward-call generation.
- Added parser error recovery and statement-token result validation.

## 0.1.5

- Replaced the Python frontend with a tokenized statement lexer and recursive-descent parser.
- Added the current subset of functions, conditionals, `while`, `for ... else`, return, loop control, assignment, and print.
- Expressions continue to use the unified precedence parser, preventing inconsistencies between statement and expression syntax.
- Added diagnostics and recovery for invalid chained assignment, parameter forms, and isolated clauses.
- Added statement-token validation across CPython, JavaScript, and C++.

## 0.1.4

- Python now supports implicit continuation inside delimiters, backslash continuation, tab indentation, and multiple simple statements on one line.
- Matlab now supports `...` continuation, multiline matrices, block comments, and multiple statements on one line.
- Python and Matlab comments and strings can now be processed safely across logical lines.
- Added diagnostics for unclosed delimiters, strings, comments, and invalid continuations.
- Added JavaScript and C++ result validation for multiline source programs.

## 0.1.3

- Python conditions and loops now support numeric, string, list, `None`, and NaN truthiness.
- Python `and`/`or` preserve operand return values, short-circuit order, and single evaluation of the left operand.
- JavaScript and C++ output preserve lazy evaluation within the supported subset.
- Python `float` now supports basic numeric, Boolean, and string conversion, including NaN and Infinity parsing.
- C++ output reports a diagnostic before generation when logical-expression result types cannot be unified statically.

## 0.1.2

- Added a unified differential-test corpus manifest.
- A single example can now compare the source language, generated JavaScript, generated C++, and declared result directly.
- Generated C++ is compiled and executed with the same compiler and generator as the top-level project.
- Generated sources, compilation logs, and results from every execution path are retained on failure.
- Automated environments pin Python and Node.js versions so missing runtimes cannot silently skip tests.

## 0.1.1

- Added independent free-form and fixed-form source normalization to Fortran.
- Free form now supports `&` continuation, comments between continued lines, character-literal continuation, and semicolon-separated statements.
- Fixed form now supports label fields, column-6 continuation, columns 7–72 for statements, and traditional full-line comments.
- Source form is selected automatically from the file extension or explicitly through the API or `--fortran-form`.
- Added diagnostics for isolated continuations, unfinished continuations, invalid character continuation, and unsafe fixed-form layouts.

## 0.1.0

- Added multi-file source management with stable source-location identities.
- All diagnostics now include filenames, start and end positions, and source excerpts.
- Added public API support for text diagnostic rendering and JSON diagnostics v1 output.
- Added `--diagnostics-format text|json` to the CLI.
- Unified CLI exit statuses for compilation, argument, input, and output failures.
- Installed the diagnostics v1 JSON Schema and added tool-integration documentation.

## 0.0.9

- Python now supports variable-length replacement for ordinary slices and equal-length assignment for extended slices.
- Matlab now supports row, column, rectangular block, and column-major linear section assignment.
- Fortran now supports one- and two-dimensional array-section assignment, shape checking, and scalar expansion.
- Added corresponding in-place section updates to the JavaScript and C++ runtimes.
- Added diagnostics for writes to temporary sections, shape mismatches, and dynamic container changes that C++ cannot preserve.

## 0.0.8

- Split the JavaScript and C++ backends into independent components that can be enabled or disabled separately.
- Generating either target no longer requires the other target backend to be built.
- Added backend-availability queries to the public API and explicit diagnostics when a requested backend was not built.
- Added `core`, `javascript`, and `cpp` components to the CMake install package.
- Added build, install, and external-consumer validation for JavaScript-only, C++-only, and core-only configurations.
- Fixed lost rank information for ragged Python lists.

## 0.0.7

- Python now supports positive- and negative-stride slices and deeper rectangular nested-list shape inference.
- Matlab now supports whole rows, whole columns, two-dimensional blocks, strided colon expressions, and `A(:)`.
- Fortran now supports one- and two-dimensional array sections with default bounds and positive or negative strides.
- Added general section-read support to the JavaScript and C++ runtimes.
- Improved handling of empty sections, dynamic extents, per-dimension bounds, and fixed-shape assignment.

## 0.0.6

- Python now supports rectangular nested lists and two-dimensional indexed reads and writes.
- Matlab now supports matrix literals, two-dimensional indexing, and column-major linear indexing.
- Fortran now supports two-dimensional constant extents, rank checking, and one- or two-dimensional `RESHAPE`.
- Added multi-index access, recursive aggregation, and column-major reshape to the JavaScript and C++ runtimes.
- C++ output now safely diagnoses ragged lists.

## 0.0.5

- Python now supports one-dimensional list reads and writes, negative indices, `len`, and `sum`.
- Matlab now supports row vectors, one-based indexing, `length`, `numel`, and `sum`.
- Fortran now supports one-dimensional fixed-size arrays, array constructors, one-based indexing, `SIZE`, and `SUM`.
- Added static-extent, homogeneous-element, constant out-of-bounds, and index-type checks.
- Added corresponding bounds and index-base handling to the JavaScript and C++ runtimes.

## 0.0.4

- Added Python `elif`, Matlab `elseif`, and Fortran `ELSE IF`.
- Added Python/Matlab `break` and `continue`, along with Fortran `EXIT` and `CYCLE`.
- Added Python `for ... else` and `while ... else`, including correct `break` behavior in nested loops.
- Improved loop and function context checks, unreachable-code warnings, and return-path analysis.
- C++ output now rejects incompatible return types and combinations of value returns with implicit empty returns.

## 0.0.3

- Added name binding, built-in name shadowing, undefined-identifier checks, and definite-assignment analysis.
- Added basic type inference for integers, real numbers, Booleans, and strings.
- Added Python `range`/`while`, Matlab colon `for`/`while`, and Fortran `DO`/`DO WHILE`.
- Preserved each language's final loop-variable semantics and added negative-step support.
- Added safe rewriting of reserved words and name collisions to JavaScript and C++ output.
- Generated C++ code is now isolated in the `mpf_generated` namespace.

## 0.0.2

- Added JavaScript/C++ output-target selection to the public API and CLI.
- Added an independent C++17 backend, basic runtime, and executable entry-point generation.
- Added UTF-8 source locations, CRLF handling, and a shared token model.
- Python, Matlab, and Fortran expressions now use structured lexers and parsers.
- JavaScript output now supports safe operator precedence, Python floor division, and list/tuple values.
- Added Node.js validation for generated JavaScript and real compile-and-execute tests for generated C++.

## 0.0.1

- Established the MPF public API, command-line tool, basic scalar transpilation for three languages, JavaScript backend, test infrastructure, and build system.
