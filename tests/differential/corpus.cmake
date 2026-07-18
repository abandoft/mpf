# name, source language, repository-relative input, normalized output oracle
mpf_add_differential_case(typescript-basic typescript examples/typescript/basic.ts "42")
mpf_add_differential_case(typescript-arrays typescript examples/typescript/arrays.ts "42")
mpf_add_differential_case(typescript-block-scopes typescript examples/typescript/block_scopes.ts "inner 42")
mpf_add_differential_case(typescript-for-loops typescript examples/typescript/for_loops.ts "13")
mpf_add_differential_case(fortran-basic fortran examples/fortran/basic.f90 "42")
mpf_add_differential_case(fortran-statement-tokens fortran examples/fortran/statement_tokens.f90 "42")
mpf_add_differential_case(fortran-procedures fortran examples/fortran/procedures.f90 "120 42 42 42 42 2 1")
mpf_add_differential_case(fortran-reference-arguments fortran examples/fortran/reference_arguments.f90 "42 42 42 42 42 41")
mpf_add_differential_case(fortran-array-reference-arguments fortran examples/fortran/array_reference_arguments.f90 "2 4 6 20 22 42")
mpf_add_differential_case(fortran-section-reference-arguments fortran examples/fortran/section_reference_arguments.f90 "42 11 14 24 15 53 20 22 30 32 40 42")
mpf_add_differential_case(fortran-disjoint-regions fortran examples/fortran/disjoint_regions.f90 "40 2 41 1")
mpf_add_differential_case(fortran-argument-association fortran examples/fortran/argument_association.f90 "42 41 42 41 42 30")
mpf_add_differential_case(fortran-optional-writeback fortran examples/fortran/optional_writeback.f90 "44 22 2 6 20 22 5 20 22 42 42")
mpf_add_differential_case(fortran-select-case fortran examples/fortran/select_case.f90 "7 42")
mpf_add_differential_case(python-basic python examples/python/basic.py "42")
mpf_add_differential_case(python-optimization python examples/python/optimization.py "9")
mpf_add_differential_case(python-scalars python examples/python/scalars.py "answer 3.5" lines)
mpf_add_differential_case(python-truthiness python examples/python/truthiness.py "1848 1 0 2 2")
mpf_add_differential_case(python-logical-lines python examples/python/logical_lines.py "43")
mpf_add_differential_case(python-statement-tokens python examples/python/statement_tokens.py "42")
mpf_add_differential_case(python-function-graph python examples/python/function_graph.py "120 42")
mpf_add_differential_case(python-parameter-association python examples/python/parameter_association.py "42 84 82 82 41 42 42 answer?")
mpf_add_differential_case(python-unpacking python examples/python/unpacking.py "41 40 42 42 answer 42 42 42 42")
mpf_add_differential_case(python-structured-unpacking python examples/python/structured_unpacking.py "10 20 43 23 42 1 5 4 0 42")
mpf_add_differential_case(python-expression-semantics python examples/python/expression_semantics.py "2 2 40 42 2 3 7 8")
mpf_add_differential_case(python-comparisons python examples/python/comparisons.py "10 20 3 4 5 6 7 9 8")
mpf_add_differential_case(matlab-logical-lines matlab examples/matlab/logical_lines.m "42")
mpf_add_differential_case(matlab-statement-tokens matlab examples/matlab/statement_tokens.m "42")
mpf_add_differential_case(matlab-multi-output matlab examples/matlab/multi_output.m "97")
mpf_add_differential_case(matlab-function-graph matlab examples/matlab/function_graph.m "61")
mpf_add_differential_case(matlab-switch-case matlab examples/matlab/switch_case.m "42")
mpf_add_differential_case(matlab-operators matlab examples/matlab/operators.m "101")
mpf_add_differential_case(matlab-matrix-solve matlab examples/matlab/matrix_solve.m "35.4")
mpf_add_differential_case(
  matlab-structured-square-solve matlab examples/matlab/structured_square_solve.m
  "3 2 1 2 3 2 1.9")
mpf_add_differential_case(
  matlab-advanced-structured-square-solve matlab
  examples/matlab/advanced_structured_square_solve.m
  "1 2 3 1 2 3 1 2 3 1 2 3 1 2 3")
mpf_add_differential_case(
  matlab-singular-tridiagonal-warning matlab examples/matlab/singular_tridiagonal_warning.m "1"
  tokens "matrix is singular to working precision")
mpf_add_differential_case(
  matlab-nearly-singular-cholesky-warning matlab
  examples/matlab/nearly_singular_cholesky_warning.m "1" tokens
  "matrix is close to singular or badly scaled")
mpf_add_differential_case(
  matlab-structured-condition-warnings matlab examples/matlab/structured_condition_warnings.m
  "3" tokens "matrix is close to singular or badly scaled" 3)
mpf_add_differential_case(
  matlab-singular-square-solve matlab examples/matlab/singular_square_solve.m "8" tokens
  "matrix is singular to working precision" 2)
mpf_add_differential_case(
  matlab-nearly-singular-square-solve matlab examples/matlab/nearly_singular_square_solve.m
  "4" tokens "matrix is close to singular or badly scaled")
mpf_add_differential_case(
  matlab-rectangular-solve matlab examples/matlab/rectangular_solve.m "4 7 0 3 6 2 3 1 1")
mpf_add_differential_case(
  matlab-rank-deficient-solve matlab examples/matlab/rank_deficient_solve.m "0 1" tokens
  "rank deficient to working precision")
mpf_add_differential_case(matlab-implicit-expansion matlab examples/matlab/implicit_expansion.m "329")
mpf_add_differential_case(
  matlab-dynamic-broadcast matlab examples/matlab/dynamic_broadcast.m "80 127 2")
mpf_add_differential_case(matlab-transpose matlab examples/matlab/transpose.m "16")
mpf_add_differential_case(matlab-end-indexing matlab examples/matlab/end_indexing.m "122")
mpf_add_differential_case(matlab-dynamic-end matlab examples/matlab/dynamic_end.m "260")
mpf_add_differential_case(matlab-logical-indexing matlab examples/matlab/logical_indexing.m "48")
mpf_add_differential_case(matlab-logical-comparisons matlab examples/matlab/logical_comparisons.m "164")
mpf_add_differential_case(
  matlab-logical-semantics matlab examples/matlab/logical_semantics.m "2 5 1 3 10 0 10 20 1 1")
mpf_add_differential_case(
  matlab-ieee-division matlab tests/fixtures/matlab_ieee_division.m "2")
mpf_add_differential_case(
  matlab-logical-reduction matlab examples/matlab/logical_reduction.m "2 5 3 3 3 6 3 5")
mpf_add_differential_case(
  matlab-generalized-indexing matlab examples/matlab/generalized_indexing.m "40 20 20 0 9 10 90 7")
mpf_add_differential_case(
  matlab-shape-mutation matlab examples/matlab/shape_mutation.m "0 9 8 6 30 6 9 3 0 9 7 30")
mpf_add_differential_case(
  matlab-empty-arrays matlab examples/matlab/empty_arrays.m "0 0 3 7 5 0 5 0 5 0 5 0 4 9")

mpf_add_differential_case(fortran-loops fortran examples/fortran/loops.f90 "12 -1")
mpf_add_differential_case(python-loops python examples/python/loops.py "12 1")
mpf_add_differential_case(matlab-loops matlab examples/matlab/loops.m "12")

mpf_add_differential_case(python-names python examples/python/names.py "42")
mpf_add_differential_case(python-globals python examples/python/globals.py "42")
mpf_add_differential_case(python-control-flow python examples/python/control_flow.py "8 5 13 13 1")
mpf_add_differential_case(matlab-control-flow matlab examples/matlab/control_flow.m "13")
mpf_add_differential_case(fortran-control-flow fortran examples/fortran/control_flow.f90 "8 5")
mpf_add_differential_case(python-nested-loops python examples/python/nested_loops.py "1 1 0")

mpf_add_differential_case(python-arrays python examples/python/arrays.py "3 16 5 10 1")
mpf_add_differential_case(matlab-arrays matlab examples/matlab/arrays.m "24")
mpf_add_differential_case(fortran-arrays fortran examples/fortran/arrays.f90 "3 16 5")
mpf_add_differential_case(python-matrices python examples/python/matrices.py "2 3 7")
mpf_add_differential_case(matlab-matrices matlab examples/matlab/matrices.m "15")
mpf_add_differential_case(fortran-matrices fortran examples/fortran/matrices.f90 "4 15 7")
mpf_add_differential_case(fortran-tensors fortran examples/fortran/tensors.f90 "8 67 42 40 2")

mpf_add_differential_case(python-sections python examples/python/sections.py "3 9 3 9")
mpf_add_differential_case(python-tensors python examples/python/tensors.py "2 2 7 9")
mpf_add_differential_case(matlab-sections matlab examples/matlab/sections.m "34")
mpf_add_differential_case(fortran-sections fortran examples/fortran/sections.f90 "32")
mpf_add_differential_case(python-assignments python examples/python/assignments.py "5 36")
mpf_add_differential_case(matlab-assignments matlab examples/matlab/assignments.m "19")
mpf_add_differential_case(fortran-assignments fortran examples/fortran/assignments.f90 "19")

mpf_add_differential_case(fortran-source-forms fortran examples/fortran/source_forms.f90 "42")
mpf_add_differential_case(fortran-fixed-form fortran examples/fortran/fixed_form.f "42")
