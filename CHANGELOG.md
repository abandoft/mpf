## 0.7.5

- Matlab `try`/`catch` now translates to native structured exception handling in JavaScript and C++17.
- Catch clauses support either an exception binding or the binding-free `catch` form.
- Successful protected regions skip their handlers, while runtime failures enter the matching handler immediately.
- `error(message)` and `error(identifier, message)` now raise catchable Matlab exceptions in both targets.
- Caught exceptions expose stable `identifier` and `message` properties across JavaScript and C++17.
- `rethrow(exception)` now preserves the original exception when forwarding it to an outer handler.
- Nested protected regions select the innermost handler and can propagate failures to enclosing handlers.
- JavaScript runtime errors from translated operations are normalized into immutable Matlab exception values.
- C++17 generated code preserves Matlab exception identifiers and messages without depending on JavaScript output.
- Definite-assignment analysis prevents a catch-only exception binding from being read after a normally completed `try` path.
- Source maps now retain separate locations for `try`, `catch`, protected statements, and handler statements.
- Malformed, missing, or repeated catch clauses and invalid `error`/`rethrow` arguments now fail with focused diagnostics.

## 0.7.4

- Matlab command syntax can now call supported local functions and built-ins, passing each command argument as a character vector.
- Multi-argument commands preserve spaces grouped by single quotes and decode doubled apostrophes inside those groups.
- Double-quote characters remain part of command text, while whitespace continues to delimit command arguments according to Matlab rules.
- Operator spacing now distinguishes command text such as `name +value` and `name ./path` from expression forms such as `name + value` and `name ./ path`.
- Values returned by command-form calls are assigned to Matlab's implicit `ans` variable.
- Commands that call functions without outputs execute without replacing an existing `ans` value.
- Definite-assignment analysis now tracks `ans` across branches and diagnoses reads when no value-producing command is guaranteed to run.
- Command calls to multi-output Matlab functions store the first output in `ans`.
- `length` and `numel` now accept character-vector values in generated JavaScript and C++17.
- The C++17 backend now rejects incompatible or unprovable `ans` declaration types across reassignment and control-flow paths before emitting uncompilable assignments.
- Unknown command callees and unterminated quoted command arguments now fail with focused source diagnostics.
- Source maps retain every value-producing and output-free command call in both generated targets.

## 0.7.3

- Matlab functions can now use bare `return` to exit before normal fallthrough.
- Early return preserves a function's declared single output value.
- Multi-output functions return every declared output in declaration order when exiting early.
- Matlab scripts can stop before later executable statements with bare `return`.
- File-level local functions remain available when a script uses early return.
- Generated JavaScript uses a collision-safe labeled script region while keeping local functions at module scope.
- Generated C++17 exits the generated program entry cleanly for Matlab script return.
- Matlab command syntax now supports a single character-vector argument for both `disp` and `display`.
- Unquoted, single-quoted, and double-quoted command input follows Matlab character-vector rules, including spaces and preserved double-quote characters.
- Invalid return operands and multi-argument command forms now fail with focused diagnostics.
- Unreachable-code warnings distinguish skipped script statements from file-level local function declarations.
- Source maps retain the original locations of function and script return statements.

## 0.7.2

- Matlab complex sparse matrices can now participate in statically shaped rank-two matrix multiplication.
- Complex sparse-by-sparse products preserve canonical CSC storage and complex values.
- Sparse-by-dense and dense-by-sparse complex products return dense matrices without first materializing the sparse operand.
- Real and logical sparse operands are promoted when multiplied with complex sparse or dense matrices.
- Sparse product kernels traverse stored entries directly and use a column-wise accumulator for sparse results.
- Exact-zero complex cancellations are omitted from CSC output instead of becoming stored entries.
- Zero-row, zero-column, and empty-inner-dimension products preserve their planned Matlab shapes and storage classes.
- Generated runtimes reject inconsistent shape or numeric-domain plans before performing a product.
- Nonfinite complex inputs and multiplication overflow fail with stable generated-runtime errors.
- Generated C++17 preserves typed real or `std::complex<double>` result storage across all supported operand combinations.
- Matrix-product runtime support is emitted only for programs that use it, reducing generated code for unrelated sparse operations.
- Source maps retain the original locations of complex sparse matrix-product expressions.

## 0.7.1

- Matlab complex sparse matrices now support compatible-size addition and subtraction.
- Adding or subtracting two sparse operands preserves canonical CSC storage and complex values.
- Real and logical sparse operands are promoted when combined with a complex sparse matrix.
- Mixed sparse and dense arithmetic produces dense complex results while traversing sparse inputs by stored entries.
- Complex scalar addition and subtraction work on either side of a sparse matrix with the correct operand order.
- Singleton sparse rows and columns participate in Matlab compatible-size expansion without dense source conversion.
- Complex sparse arithmetic preserves explicit zero extents and the sparse storage class of sparse-only results.
- Exact-zero complex cancellations are removed from CSC results instead of remaining as stored entries.
- Nonfinite complex operands and arithmetic overflow fail with stable generated-runtime errors.
- Generated JavaScript carries an explicit complex value-domain tag, while generated C++17 preserves typed `std::complex<double>` sparse storage.
- Complex-number and complex-sparse runtime support is emitted only when the translated program requires it.
- Source maps retain the original locations of complex sparse addition and subtraction expressions.

## 0.7.0

- Matlab `sparse(A)` now preserves statically shaped binary64 complex rank-two values in canonical CSC storage.
- Complex values are supported by inferred, explicitly sized, and `nzmax` R2024 sparse triplet constructors.
- Duplicate complex triplets are accumulated component-wise, and exact-zero cancellation removes the stored entry.
- Converting an existing complex sparse matrix with `sparse(A)` preserves its shape, values, and sparse representation.
- Ordinary sparse transpose (`.'`) preserves complex values without conjugation, while conjugating transpose (`'`) negates imaginary components.
- Scalar, linear, and Cartesian submatrix indexing preserve complex values and CSC storage where the result remains nonscalar.
- Sparse reshape preserves complex values and Matlab column-major order without materializing a dense source matrix.
- Indexed complex assignment can update or grow canonical CSC storage, and assigning exact zero removes the corresponding entry.
- `full`, `issparse`, and `nnz` now operate on supported complex sparse matrices with the expected value and storage behavior.
- Generated JavaScript uses an explicit complex sparse value domain; generated C++17 uses `sparse_matrix<std::complex<double>>`.
- The target-owned complex sparse runtime fragment is emitted only when the program contains an actual complex CSC value.
- Generated runtimes reject nonfinite complex entries and nonfinite duplicate accumulation with stable errors.
- JavaScript output now preserves Matlab value semantics when an aliased dense or sparse array is mutated, including function parameters.

## 0.6.9

- Matlab `sparse(A)` now accepts statically shaped logical rank-two arrays and produces typed logical CSC matrices.
- Logical triplet constructors are supported across the inferred, explicitly sized, and `nzmax` R2024 call forms.
- Duplicate logical triplets now follow Matlab semantics by combining values with `any` instead of numeric addition.
- False logical values are omitted from canonical CSC storage rather than retained as explicit entries.
- Ordinary and conjugating sparse transpose preserve the logical value class.
- Scalar, linear, and Cartesian submatrix selection preserve logical sparse storage and result type where applicable.
- Indexed assignment, growth, and deletion preserve logical CSC matrices, with false assignments removing stored entries.
- Sparse reshape preserves logical values, Matlab column-major order, inferred dimensions, and zero extents.
- `full` returns logical dense arrays for logical sparse inputs, while `issparse` and `nnz` retain their expected behavior.
- Generated JavaScript carries an explicit sparse value-domain tag and rejects malformed logical CSC values before use.
- Generated C++17 uses `sparse_matrix<bool>` for logical CSC values and safely handles packed logical-vector storage.
- JavaScript and C++17 use independent typed sparse runtimes and do not require one target to be generated before the other.
- Fixed sparse square solves with sparse right-hand sides after introducing the typed sparse conversion ABI.
- Sparse assignment failures now use consistent real-or-logical diagnostics across generated JavaScript and C++17.

## 0.6.8

- Matlab sparse matrices now preserve zero extents throughout the supported static, finite-real rank-two feature set.
- All R2024 `sparse` constructor forms retain canonical CSC storage and explicit shapes when either dimension is zero.
- Sparse transpose, indexing, assignment, growth, deletion, and reshape now preserve shaped-empty matrices without dense materialization.
- Sparse-by-sparse and mixed-storage matrix products support empty output dimensions and empty inner dimensions with Matlab-compatible result storage.
- Bidirectional sparse/scalar scaling and compatible-size sparse `.*` operations now preserve zero-extent shapes and canonical CSC results.
- Sparse left division supports a `0×0` coefficient matrix with dense or sparse `0×n` right-hand sides.
- Sparse right division supports dense or sparse `m×0` left-hand sides with a `0×0` coefficient matrix.
- Zero-dimensional square solves preserve the source-language storage rule: dense operands produce shaped dense results and CSC operands produce canonical CSC results.
- Generated JavaScript and C++17 validate coefficient, operand, and result shape plans before executing a sparse solve and report stable errors for corrupted plans.
- JavaScript and C++17 use independent sparse runtimes and return zero-dimensional results without factorization or conversion through the other target.
- Source maps preserve every new sparse operation and zero-dimensional left- or right-division call site.

## 0.6.7

- Matlab sparse matrices now support compatible-size element-wise multiplication with `.*` within the nonempty, statically shaped, finite-real rank-two contract.
- Supported operand forms cover sparse-by-scalar, scalar-by-sparse, sparse-by-dense, dense-by-sparse, and sparse-by-sparse products.
- Singleton row and column dimensions are expanded according to Matlab compatible-array-size rules.
- Sparse element-wise products preserve the computed shape and return canonical CSC storage for every supported operand form.
- JavaScript and C++17 use independent nonzero-driven kernels that avoid materializing sparse operands as dense matrices.
- Sparse-by-sparse products merge CSC columns directly, while mixed products inspect only stored sparse entries and the corresponding dense values.
- Exact-zero products, including cancellations caused by underflow, are removed from the resulting CSC matrix.
- Generated runtimes reject malformed CSC values, inconsistent shape plans, nonfinite inputs, and nonfinite results with stable errors.
- Source maps preserve the original `.*` expression location for every sparse operand arrangement.
- Complex, zero-extent, dynamically shaped, and incompatible sparse element-wise operations continue to fail closed with diagnostics instead of changing semantics.
- Added an executable Matlab example that compares values, shape, and sparse storage across both output targets.

## 0.6.6

- Matlab sparse matrices can now be multiplied by a scalar on either side with `A * factor` and `factor * A`.
- Sparse scalar products preserve the source dimensions and return canonical CSC storage.
- Multiplication by zero returns an empty CSC matrix of the same shape without scanning stored values.
- Negative and logical scalar factors are supported within the static finite-real rank-two sparse contract.
- Values that underflow to exact zero are removed from the result instead of being retained as explicit sparse entries.
- Nonfinite scalar factors and nonfinite multiplication results fail with stable runtime errors in generated JavaScript and C++17.
- JavaScript and C++17 use independent nonzero-driven scaling kernels and do not materialize a dense source matrix.
- Source maps now preserve both right-scalar and left-scalar sparse product call sites.

## 0.6.5

- Matlab now supports nonempty, statically shaped, finite-real rank-two sparse-by-sparse matrix multiplication.
- Sparse-by-dense and dense-by-sparse Matlab matrix products are now supported without converting the sparse operand to a dense source matrix.
- Sparse-by-sparse products return canonical CSC matrices, while both mixed-storage products return dense matrices in line with Matlab storage behavior.
- JavaScript and C++17 use independent sparse matrix-product runtimes and target-specific helper bindings.
- The CSC-by-CSC kernel uses a column-wise sparse accumulator, sorts only touched rows, and removes exact-zero cancellations.
- Mixed-storage kernels traverse CSC nonzero entries directly and accumulate into dense results.
- Generated runtimes validate canonical CSC structure, finite values, rectangular dense operands, and multiplication dimensions before computing a result.
- Unsupported sparse scalar multiplication, complex sparse products, zero extents, dynamic sparse shapes, and incompatible dimensions continue to fail closed with stable diagnostics.
- Sparse matrix-product calls retain their original locations in JavaScript and C++17 source maps.
- Added an executable Matlab example covering sparse-by-sparse and both mixed-storage product forms.

## 0.6.4

- Matlab sparse matrices can now be reshaped with either a size vector or a comma-separated dimension list.
- Comma-separated reshape dimensions accept one `[]` placeholder and infer its extent from the source element count.
- Sparse reshape requests with more than two dimensions follow Matlab behavior by preserving the first extent and folding the remaining extents into the second.
- Reshaped sparse matrices preserve Matlab column-major linear order and remain canonical CSC values.
- Generated JavaScript and C++17 remap CSC entries directly in O(nnz + output columns), without sorting entries or materializing a dense matrix.
- JavaScript and C++17 use independent sparse-reshape runtimes and do not consume artifacts from the other backend.
- Generated runtimes validate canonical CSC input and all serialized shape plans before producing a result.
- Invalid element counts, multiple inferred dimensions, zero extents, and unsupported dynamic, empty, or complex sparse sources fail closed with stable diagnostics.
- Dense Matlab reshape also accepts one inferred dimension in the comma-separated form when the source element count is statically known.
- Sparse-reshape calls retain their original source locations in JavaScript and C++17 source maps.

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
- Added an executable sparse-assignment example covering insertion, overwrite, zero removal, repeated indices, growth, deletion, sparse replacement, and self-aliasing on both targets.

## 0.6.2

- Matlab sparse matrices now support read-only scalar access through one linear index or two row/column subscripts.
- Sparse selection accepts full colon, positive or negative-step slices, ordered or repeated numeric selectors, logical selectors, and empty selectors.
- Linear sparse results follow Matlab shape rules: numeric matrix selectors retain their shape, logical matrices and full colon produce columns, and vector/vector indexing retains the source orientation.
- Two-subscript selection forms the Matlab Cartesian product with a `numel(rows) × numel(columns)` result.
- Nonscalar sparse indexing preserves canonical CSC storage and never materializes a dense copy of the source matrix.
- Full-colon selection remaps CSC entries directly in O(nnz), while submatrix selection scans chosen columns through an ordered row map.
- Generated JavaScript and C++17 use independent checked sparse-index runtimes; neither target depends on artifacts from the other backend.
- `full` now accepts rank-two dense or CSC selections with dynamic or zero result extents when their storage representation is known.
- Sparse assignment, more than two selectors, complex or sparse selectors, N-dimensional linear results, and dynamic, empty, or complex sparse sources continue to fail closed with stable diagnostics.
- Sparse-indexing calls retain their original locations in both target source maps, and out-of-bounds access reports stable errors.

## 0.6.1

- Matlab now accepts every R2024 `sparse` call form within MPF's nonempty, statically shaped real rank-two contract: `sparse(A)`, `sparse(m,n)`, inferred triplets, explicitly sized triplets, and the `nzmax` form.
- `sparse(m,n)` and explicitly sized empty triplets now create zero-valued canonical CSC matrices without materializing dense storage.
- Triplet construction supports scalar expansion for row indices, column indices, or values while requiring all nonscalar inputs to have equal element counts.
- Inferred triplet construction derives its output dimensions from positive compile-time literal indices; explicitly sized construction validates known and runtime indices against the requested shape.
- Duplicate triplet coordinates are accumulated deterministically in column-major order, and exact cancellation removes the stored entry.
- The `nzmax` argument is validated as a nonnegative compile-time integer and becomes a capacity hint in generated C++17 without changing observable matrix contents.
- Generated JavaScript and C++17 build canonical sorted CSC data directly from triplets instead of allocating an intermediate dense matrix.
- Ordinary and conjugating transpose now preserve sparse CSC storage for the delivered real sparse contract and use target-owned transpose helpers.
- Sparse constructor and transpose source locations remain mapped through both target-specific runtime calls; neither output backend depends on the other's artifacts.
- Runtime checks now reject fractional or nonpositive triplet indices, mismatched nonscalar triplets, out-of-range coordinates, invalid dimensions, and nonfinite duplicate accumulation with stable diagnostics.

## 0.6.0

- Matlab `sparse(A)` now converts nonempty, statically shaped real rank-two arrays into canonical compressed sparse column storage.
- Matlab `full`, `issparse`, and `nnz` now support the delivered dense/CSC conversion, storage-query, and nonzero-count workflows.
- Sparse real square left division now supports vectors and multiple right-hand-side columns in generated JavaScript and C++17.
- Tridiagonal sparse coefficients use a compact O(nnz) factorization, while other square patterns use partial-row-pivoted sparse LU without creating a dense coefficient copy.
- Sparse square right division reuses the transposed sparse solve and preserves sparse results according to Matlab left/right operand rules.
- Exact singular and nearly singular sparse systems now emit the same stable condition warnings as the existing dense square solvers.
- Generated JavaScript uses a private tagged CSC value, while generated C++17 uses a typed `mpf_runtime::sparse_matrix`; the two runtimes remain independent.
- Runtime validation rejects malformed CSC pointers, unordered row indices, zero or nonfinite stored entries, and incompatible solve shapes.
- Unsupported sparse constructors, indexing, transpose, reshape, logical, element-wise, multiplication, power, rectangular, complex, and zero-extent cases fail before emission with `MPF2054`.
- Numeric type planning now preserves Matlab binary64 arrays while keeping Python operand-returning short-circuit and conditional-expression result types correct in generated C++17.

## 0.5.9

- Matlab complex rectangular left division now supports overdetermined and underdetermined systems in generated JavaScript and C++17.
- Complex rectangular right division follows Matlab's conjugate-transpose solve identity for both solve shapes.
- Multiple right-hand-side columns share one complex column-pivoted QR factorization.
- Rank-revealing complex Householder QR uses deterministic column pivoting and a working-precision rank tolerance.
- Rank-deficient complex systems return a pivoted basic least-squares solution and emit a stable warning instead of failing silently.
- JavaScript and C++17 retain independent complex rectangular runtimes with finite-value, rectangularity, shape, and solve-plan validation.
- Source maps preserve complex overdetermined, underdetermined, left-division, and right-division operator locations.

## 0.5.8

- Matlab complex rank-two matrices now support true matrix multiplication in generated JavaScript and C++17.
- Dense complex square left division now supports vector or multiple-column right-hand sides.
- Complex square right division follows the conjugate-transpose solve identity instead of reusing real transpose semantics.
- Exact Hermitian positive-definite coefficient matrices now use a dedicated complex Cholesky factorization and solve path.
- Non-Hermitian and non-positive-definite complex square systems fall back to magnitude-pivoted dense LU deterministically.
- Complex square solvers now estimate reciprocal condition and preserve the existing singular and nearly-singular warning behavior.
- Complex square matrix power supports zero, positive, and negative ECMAScript-safe integer exponents through exponentiation by squaring.
- Generated runtimes reject malformed, nonfinite, shape-inconsistent, or fractional complex matrix operations at their owned boundaries.
- JavaScript and C++17 use independent complex-matrix runtimes; neither target reads generated code or runtime artifacts from the other.
- Complex-matrix runtime fragments are dependency-checked and omitted entirely from real-only matrix programs.
- Source maps retain the original locations of complex multiplication, left/right division, and positive or negative matrix powers.

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
- Source maps now retain source locations for complex construction, arithmetic, array mutation, reshape, and transpose operations.
- Python scalar return annotations now guide generated C++17 types, preventing annotated integer functions from being emitted with incompatible inferred types.
- Fixed Fortran optional writable arguments and Python truthiness behavior in both output backends.

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
- Generated C++17 scalar division uses portable runtime calls for Matlab and TypeScript IEEE results, while Python true and floor division report stable zero-denominator errors in both targets.
- Source maps cover logical and reduction runtime calls; character arrays, dynamic dimensions, duplicate or invalid dimensions, and unsupported unknown-rank reductions produce stable diagnostics.

## 0.5.5

- Matlab square left and right division now detects full real tridiagonal coefficient matrices, matching the structure introduced for dense inputs in Matlab R2024a.
- Tridiagonal systems now use an adjacent-row partial-pivoting LU factorization with compact diagonal storage instead of general dense LU.
- The tridiagonal runtime supports multiple right-hand sides and dedicated transpose solves, so right division uses the same specialized contract.
- Exact symmetric positive-definite matrices now use Cholesky factorization and triangular substitution in generated JavaScript and C++17.
- Symmetric matrices that are not positive definite fall back deterministically to partial-pivoting dense LU without exposing a failed Cholesky result.
- Square structure selection now follows diagonal, triangular, tridiagonal, symmetric-positive-definite, and dense fallback priority in both output targets.
- Tridiagonal and Cholesky kernels now participate in the same iterative 1-norm reciprocal-condition estimation used for square-system warnings.
- Exact singular tridiagonal systems and nearly singular positive-definite systems emit the stable Matlab-style warning and continue with the computed result.

## 0.5.4

- Matlab square left and right division now detects diagonal, upper-triangular, lower-triangular, and general dense real coefficient matrices at runtime.
- Diagonal systems now use direct element-wise solves instead of allocating and factoring a dense LU matrix.
- Upper- and lower-triangular systems now use dedicated forward or backward substitution in generated JavaScript and C++17.
- General square systems continue to use partial-pivoting dense LU as the deterministic fallback.
- Matlab right division classifies the transposed divisor and uses the same structure-aware solver contract as left division.
- Reciprocal-condition estimation now follows the selected diagonal, triangular, or dense kernel, including transpose solves required by the estimator.
- Exact singular and nearly singular structured systems emit the existing stable Matlab-style warnings and continue with the computed IEEE result.
- Generated C++17 now recursively widens mixed integer and floating-point matrix rows to one concrete numeric container type.
- Source maps now retain the original locations of structured left and right division expressions.

## 0.5.3

- Matlab rectangular left and right division now use rank-aware column-pivoted QR for both overdetermined and underdetermined systems.
- Underdetermined solves now return a pivoted basic solution instead of the previously incorrect minimum-norm result.
- Rank-deficient rectangular systems continue with a stable working-precision warning and a basic least-squares result.
- Exact singular square left and right division now warn and continue, preserving Matlab-style finite and IEEE-infinity components instead of terminating generated programs.
- Nearly singular square systems now estimate reciprocal condition with the existing LU factors, emit a distinct `RCOND` warning, and continue with the computed result.
- Generated JavaScript and C++17 now own independent partial-pivoted LU, transpose-solve, condition-estimation, and column-pivoted QR runtimes.
- Invalid non-vector linear deletion remains rejected consistently, while singular-square execution is no longer misclassified as an unsupported runtime operation.
- Source maps now retain both left-division and right-division locations for condition-aware square solves.
- Generated C++ now rejects impossible zero-extent coordinate conversion before division, keeping empty-array output warning-clean under MSVC `/WX`.
- Release SHA-256 sidecars now use a carriage-return-free format that standard Unix checksum tools can verify even when the package is built on Windows.

## 0.5.2

- Matlab `[]` now has its canonical `0×0` double-array shape instead of being treated as a rank-one empty list.
- `reshape` now accepts zero extents and preserves shapes such as `0×5`, including dimensions that cannot be recovered from nested container structure alone.
- Transpose preserves exact zero-extent shapes, so `0×N` and `N×0` arrays remain distinct in generated JavaScript and C++17.
- Scalar arithmetic and relational comparisons with empty arrays now retain the complete broadcast result shape and element type.
- Empty and colon-based section reads preserve their statically known result rank and zero extents.
- Linear assignment to `[]` follows Matlab row-vector growth semantics, while growth from shaped empty matrices retains the planned dimensions and column-major layout.
- Generated JavaScript carries exact array shape in a checked, non-enumerable descriptor, preserving normal array iteration and serialization behavior.
- Generated C++17 now consumes target-owned static shape plans for rank, `length`, transpose, broadcasting, and growth without depending on JavaScript output.
- Invalid or contradictory empty-array shapes are rejected consistently before target emission and at generated-runtime boundaries.

## 0.5.1

- Matlab indexed assignment now grows row vectors, column vectors, matrices, and N-dimensional arrays through scalar, colon-range, and ordered numeric selectors.
- Linear growth preserves Matlab column-major order and vector orientation, extends the final matrix/tensor dimension when required, and initializes gaps with the element type's default value.
- Matlab `[]` assignment now deletes vector elements or one selected dimension of a matrix/tensor, including scalar, slice, numeric, and logical selectors with duplicate positions removed once.
- Shape-changing writes support statically known bounds, runtime indexes, local-function parameters, and dynamic `end` expressions in independently generated JavaScript and C++17.
- Growth and deletion are treated as whole-storage writes, preventing stale partial-region ordering and quadratic dependency-frontier growth.
- Generated JavaScript uses checked nested-array resizing and axis deletion, while generated C++17 uses typed nested-`std::vector` templates without consuming JavaScript output.
- Both runtimes validate safe sizes, selector bounds and types, rectangular rank, replacement cardinality, and ambiguous multi-dimensional deletion at their owned boundary.
- Source maps now cover generated growth and deletion calls, and invalid linear matrix deletion, multi-axis deletion, and out-of-bounds deletion fail closed with stable diagnostics.

## 0.5.0

- Matlab compatible-size arithmetic and relational comparisons now support operands whose rank and extents are available only when a local function is instantiated or executed.
- Runtime broadcast dispatch preserves scalar results, scalar expansion, row-vector normalization, missing trailing singleton dimensions, and general rectangular nested arrays.
- Generated JavaScript derives and validates rectangular operand shapes once before its column-major flatten/stride kernel; incompatible extents fail with a stable runtime error.
- Generated C++17 combines template-known rank with runtime extents, returns the correct scalar or nested `std::vector` type, and rejects ragged or incompatible operands without depending on JavaScript output.
- C++ logical-array `sum` now returns a numeric count instead of collapsing all nonzero counts to `true`.
- Matlab `end` now works when an array extent is known only at runtime, including local-function parameters and computed selectors.
- Runtime-sized `end` supports linear column-major indexing, per-dimension indexing, colon bounds, arithmetic such as `end - 1`, and numeric selector arrays such as `[1 end]`.
- Dynamic `end` reads and writes now execute independently in generated JavaScript and C++17 for scalar elements and N-dimensional sections.
- Static extents retain their constant-folded fast path, so existing fixed-shape indexing does not pay for runtime extent resolution.
- Generated JavaScript resolves selector closures against the active axis length or column-major element count while evaluating the indexed container only once.
- Generated C++17 uses typed selector callables and a generalized column-major element accessor, including a C++17-safe type-probe path without lambdas in unevaluated operands.
- Read-only memory frontiers with no compatible function write are pruned, reducing analysis work for runtime-sized indexing and broadcasting.

## 0.4.9

- Matlab matrix left division now solves static full-rank dense real square, overdetermined, and underdetermined systems with one or more right-hand-side columns.
- Matlab matrix right division now supports row vectors and rank-two left operands with static full-rank dense real square or rectangular divisors.
- Matlab matrix power now supports zero, positive, and negative ECMAScript-safe integer exponents for static square dense real matrices.
- Square solves use partial pivoting; rectangular least-squares and minimum-norm solves use column-pivoted Householder QR in both generated JavaScript and C++17.
- Matlab array division by a scalar and scalar left division of an array now preserve matrix-operator semantics without requiring element-wise spelling.
- Matlab indexing now supports ordered numeric selector arrays, repeated indices, empty selectors, and logical selectors in either linear or per-dimension positions.
- Logical selectors with runtime-known extents now validate their shape in generated code instead of requiring every mask size to be known during compilation.
- Rank-deficient systems, non-square powers, matrix exponents, non-finite solve values, and unsafe or fractional exponents fail closed deterministically.

## 0.4.8

- Matlab array arithmetic now supports compatible-size implicit expansion across statically known N-dimensional shapes, including singleton and missing trailing dimensions.
- Matlab relational operators now produce Boolean arrays and support the same scalar, exact-shape, and compatible-size expansion rules, enabling expressions such as `A(A >= 30)`.
- JavaScript array expansion uses a flatten-once, precomputed-stride kernel, while scalar and exact-shape operations retain direct fast paths.
- Generated C++17 now provides target-independent typed implementations for N-dimensional expansion and array comparisons, matching JavaScript results.
- Matlab conjugating transpose `'` and non-conjugating transpose `.'` are parsed independently and support current real vectors and rank-two arrays.
- Matlab `end` now resolves from the active indexing dimension or linear element count when extents are statically known, including arithmetic and colon expressions.
- Matlab logical masks now support column-major linear reads and scalar or vector writes, with strict mask-size and replacement-shape validation.
- Unsupported dynamic extents, higher-rank transpose, incompatible masks, growth through `end`, matrix division, and matrix power now fail before code generation with dedicated diagnostics.

## 0.4.7

- Matlab now distinguishes matrix operators from element-wise operators such as `.*`, `./`, `.\`, and `.^`, preserving their semantics during parsing.
- Added two-dimensional numeric matrix multiplication with executable JavaScript and C++17 implementations.
- Added basic `+`, `-`, `.*`, `./`, `.\`, and `.^` operations for same-shape arrays and scalar expansion.
- Incompatible shapes and unsupported matrix division or matrix power now produce `MPF2046` instead of unreliable target code.
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

## 0.4.4

- Improved tracking of reads and writes from variables, array elements, sections, argument copies, and writeback operations.
- Propagated actual argument access regions across function calls, improving correctness checks for writable arguments and array sections.
- Improved alias analysis to distinguish definitely disjoint, definitely identical, and indeterminate memory regions.
- Fixed a case where memory-access information could become inconsistent with generated code after optimization.

## 0.4.3

- Added a unified N-dimensional array-region model supporting contiguous ranges, strided sections, negative strides, and multidimensional rectangular regions.
- Fortran now allows multiple provably disjoint sections of the same array to be passed as writable procedure arguments.
- Actual overlap and indeterminate boundaries continue to produce safety diagnostics.
- Improved handling of empty sections, large strides, and column-major linear selection.

## 0.4.2

- Added a shared optimization pipeline before the JavaScript and C++ backends so both targets consume the same optimized representation.
- Added safe integer and Boolean constant folding while preserving semantics outside JavaScript's exact integer range.
- Normalized static array shapes and removed invalid instructions and simple redundant control flow.
- Improved block-parameter copy propagation while preserving general branch joins and dynamic-value semantics.
- Compilation reports now include pre- and post-optimization sizes, constant-folding counts, and cleanup statistics.

## 0.4.1

- TypeScript `let` and `const` now support nested block scopes, name shadowing, and assignment to outer variables.
- Added standard C-style TypeScript `for` loops with initialization, conditions, updates, `break`, and `continue`.
- Fixed inaccurate local-variable scope and lifetime in generated JavaScript and C++.
- TypeScript `number` now follows ECMAScript numeric semantics, and array indexing rejects values that cannot be represented safely as integers.
- JavaScript output now includes only the runtime helpers required by the current program.

## 0.4.0

- Added a TypeScript frontend with automatic recognition of `.ts`, `.mts`, and `.cts` files.
- Initial support includes `let`/`const`, scalars and typed arrays, assignment, functions, default parameters, conditionals, `while`, loop control, and `console.log`.
- Type annotations in the supported subset can be erased when generating JavaScript or C++17.
- TypeScript `export function` declarations can generate corresponding JavaScript ESM exports.
- Added explicit diagnostics for `var`, loose equality, arrow functions, template strings, and unmodeled object semantics to prevent inconsistent generated behavior.

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

## 0.3.1

- Added Fortran `SELECT CASE`, `CASE DEFAULT`, and `END SELECT`.
- Added integer, character, and logical selectors with single values, closed intervals, and intervals with omitted bounds.
- Added validation for interval types, reversed intervals, and overlapping cases.
- Character cases now compare according to Fortran blank-padding rules.
- Improved definite-assignment and termination-flow analysis after multi-branch selections.

## 0.2.9

- Python unpacking assignment now supports nested tuple/list patterns and starred targets.
- Starred targets can appear in any position and support empty captures, nested captures, and repeated-name overwrites.
- The right-hand side is evaluated once, with assignment order preserved in JavaScript and C++ output.
- Dynamic lengths, structural mismatches, and heterogeneous starred results that C++ cannot represent now produce explicit diagnostics.

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

## 0.1.5

- Replaced the Python frontend with a tokenized statement lexer and recursive-descent parser.
- Added the current subset of functions, conditionals, `while`, `for ... else`, return, loop control, assignment, and print.
- Expressions continue to use the unified precedence parser, preventing inconsistencies between statement and expression syntax.
- Added diagnostics and recovery for invalid chained assignment, parameter forms, and isolated clauses.

## 0.1.4

- Python now supports implicit continuation inside delimiters, backslash continuation, tab indentation, and multiple simple statements on one line.
- Matlab now supports `...` continuation, multiline matrices, block comments, and multiple statements on one line.
- Python and Matlab comments and strings can now be processed safely across logical lines.
- Added diagnostics for unclosed delimiters, strings, comments, and invalid continuations.

## 0.1.3

- Python conditions and loops now support numeric, string, list, `None`, and NaN truthiness.
- Python `and`/`or` preserve operand return values, short-circuit order, and single evaluation of the left operand.
- JavaScript and C++ output preserve lazy evaluation within the supported subset.
- Python `float` now supports basic numeric, Boolean, and string conversion, including NaN and Infinity parsing.
- C++ output reports a diagnostic before generation when logical-expression result types cannot be unified statically.

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

## 0.0.1

- Established the MPF public API, command-line tool, basic scalar transpilation for three languages, and JavaScript output.
