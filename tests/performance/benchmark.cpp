#include <algorithm>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mpf/transpiler.hpp"
#include "mpf/version.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Scenario {
  std::string name;
  std::string source;
  mpf::SourceLanguage language{mpf::SourceLanguage::python};
  std::size_t minimum_memory_dependences{0};
  bool require_loop_carried_dependence{false};
};

struct Measurement {
  std::string name;
  std::size_t latency_nanoseconds{0};
  std::size_t throughput_bytes_per_second{0};
  std::size_t peak_arena_bytes{0};
  std::size_t generated_bytes{0};
};

std::string assignment_workload(const std::size_t statements) {
  std::string source = "value = 0\n";
  for (std::size_t index = 0; index < statements; ++index) source += "value = value + 1\n";
  source += "print(value)\n";
  return source;
}

std::string control_flow_workload(const std::size_t branches) {
  std::string source = "value = 0\n";
  for (std::size_t index = 0; index < branches; ++index) {
    source += "if value < " + std::to_string(index + 1U) + ":\n";
    source += "    value = value + 1\n";
    source += "else:\n";
    source += "    value = value - 1\n";
  }
  source += "print(value)\n";
  return source;
}

std::string shape_workload(const std::size_t width) {
  std::string source = "matrix = [";
  for (std::size_t row = 0; row < width; ++row) {
    if (row != 0) source += ", ";
    source += '[';
    for (std::size_t column = 0; column < width; ++column) {
      if (column != 0) source += ", ";
      source += std::to_string(row * width + column);
    }
    source += ']';
  }
  source += "]\nprint(sum(matrix[0]))\n";
  return source;
}

std::string function_workload(const std::size_t functions) {
  std::string source;
  for (std::size_t index = 0; index < functions; ++index) {
    source += "def function_" + std::to_string(index) + "(value):\n";
    source += "    return value + " + std::to_string(index) + "\n";
  }
  source += "print(function_0(1))\n";
  return source;
}

std::string typescript_workload(const std::size_t statements) {
  std::string source = "let value: number = 0;\n";
  for (std::size_t index = 0; index < statements; ++index) source += "value = value + 1;\n";
  source += "console.log(value);\n";
  return source;
}

std::string storage_region_workload(const std::size_t calls) {
  std::string source =
      "program storage_regions\n"
      "integer :: values(1024)\n";
  for (std::size_t index = 0; index < calls; ++index) {
    source += "call update(values(1:1024:2), values(2:1024:2))\n";
  }
  source +=
      "print *, values(1), values(2)\n"
      "contains\n"
      "subroutine update(odd, even)\n"
      "integer, intent(out) :: odd(:), even(:)\n"
      "odd(1) = 40\n"
      "even(1) = 2\n"
      "end subroutine update\n"
      "end program storage_regions\n";
  return source;
}

std::string memory_dependence_workload(const std::size_t loops) {
  std::string source = "value = 0\n";
  for (std::size_t index = 0; index < loops; ++index) {
    source += "while value < " + std::to_string(index + 1U) + ":\n";
    source += "    if value < " + std::to_string(index) + ":\n";
    source += "        value = value + 2\n";
    source += "    else:\n";
    source += "        value = value + 1\n";
  }
  source += "print(value)\n";
  return source;
}

std::string matlab_array_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = [";
  for (std::size_t row = 0; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += std::to_string(row * width + column + 1U);
    }
  }
  source += "];\nrow = [";
  for (std::size_t column = 0; column < width; ++column) {
    if (column != 0U) source += ' ';
    source += std::to_string(column + 1U);
  }
  source += "];\ncolumn = [";
  for (std::size_t row = 0; row < width; ++row) {
    if (row != 0U) source += "; ";
    source += std::to_string(row + 1U);
  }
  source += "];\n";
  source += "row_mask = [";
  for (std::size_t row = 0; row < width; ++row) {
    if (row != 0U) source += ' ';
    source += row % 2U == 0U ? "true" : "false";
  }
  source += "];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "matrix = matrix + row;\n";
    source += "matrix = matrix + column;\n";
  }
  source +=
      "transposed = matrix.';\n"
      "selected = transposed(transposed > row);\n";
  source += "permuted = matrix(row_mask, [" + std::to_string(width) + " 1 1]);\n";
  source += "matrix(row_mask, [1 " + std::to_string(width) + "]) = 0;\n";
  source += "disp(sum(selected))\n";
  return source;
}

std::string matlab_tensor_workload(const std::size_t pages) {
  std::string source = "values = [";
  const auto elements = pages * 16U;
  for (std::size_t index = 0; index < elements; ++index) {
    if (index != 0U) source += ' ';
    source += std::to_string(index + 1U);
  }
  source += "];\ntensor = reshape(values, 4, 4, " + std::to_string(pages) + ");\n";
  source += "offsets = reshape([";
  for (std::size_t page = 0; page < pages; ++page) {
    if (page != 0U) source += ' ';
    source += std::to_string(page + 1U);
  }
  source += "], 1, 1, " + std::to_string(pages) + ");\n";
  source += "result = tensor + offsets;\ndisp(result(end, end, end))\n";
  return source;
}

std::string matlab_logical_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "row = [";
  for (std::size_t index = 0; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += index % 3U == 0U ? "0" : std::to_string(index + 1U);
  }
  source += "];\ncolumn = [";
  for (std::size_t index = 0; index < width; ++index) {
    if (index != 0U) source += "; ";
    source += index % 4U == 0U ? "0" : std::to_string(index + 1U);
  }
  source += "];\nmask = row & column;\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "mask = (mask | row) & ~column;\n";
  }
  source +=
      "condition = 0;\n"
      "if mask\n"
      "  condition = 1;\n"
      "end\n"
      "disp(condition)\n";
  return source;
}

std::string matlab_logical_reduction_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = [";
  for (std::size_t row = 0; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += (row + column) % 7U == 0U ? "0" : "1";
    }
  }
  source += "];\nvalues = [";
  for (std::size_t index = 0; index < width * 8U; ++index) {
    if (index != 0U) source += ' ';
    source += index % 11U == 0U ? "0" : "1";
  }
  source += "];\ntensor = reshape(values, 4, 2, " + std::to_string(width) + ");\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "column_all = all(matrix);\n";
    source += "row_any = any(matrix, 2);\n";
    source += "page_all = all(tensor, [1 3]);\n";
    source += "total_any = any(tensor, 'all');\n";
  }
  source +=
      "disp(total_any || any(column_all, 'all') || any(row_any, 'all') || "
      "any(page_all, 'all'))\n";
  return source;
}

std::string matlab_dynamic_end_workload(const std::size_t rounds) {
  std::string source =
      "values = [10 20 30 40 50 60 70 80];\n"
      "matrix = [1 2 3 4; 5 6 7 8; 9 10 11 12; 13 14 15 16];\n"
      "disp(dynamic_end_kernel(values, matrix))\n"
      "function result = dynamic_end_kernel(values, matrix)\n"
      "result = 0;\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "result = result + values(end) + values(end - 1) + sum(values(2:end)) + ";
    source += "matrix(end, end) + matrix(end) + sum(values([1 end]));\n";
  }
  source += "end\n";
  return source;
}

std::string matlab_dynamic_broadcast_workload(const std::size_t rounds) {
  std::string source =
      "column = [1; 2; 3; 4];\n"
      "row = [10 20 30 40];\n"
      "disp(dynamic_broadcast_kernel(column, row))\n"
      "function result = dynamic_broadcast_kernel(left, right)\n"
      "expanded = left + right;\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "expanded = expanded + right;\n";
    source += "mask = expanded > left;\n";
  }
  source += "result = sum(expanded) + sum(mask);\nend\n";
  return source;
}

std::string matlab_shape_mutation_workload(const std::size_t rounds) {
  std::string source =
      "values = [1 2 3 4 5 6 7 8];\n"
      "matrix = [1 2 3 4; 5 6 7 8; 9 10 11 12; 13 14 15 16];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "values(end + 1) = " + std::to_string(round + 1U) + ";\n";
    source += "values(2) = [];\n";
    source += "matrix(:, end + 1) = [1; 2; 3; 4];\n";
    source += "matrix(:, 2) = [];\n";
  }
  source += "disp(values(end) + matrix(end, end))\n";
  return source;
}

std::string matlab_empty_array_workload(const std::size_t rounds) {
  std::string source;
  for (std::size_t round = 0; round < rounds; ++round) {
    const auto suffix = std::to_string(round);
    source += "empty_" + suffix + " = reshape([], 0, 32);\n";
    source.append("scaled_").append(suffix).append(" = empty_").append(suffix).append(" + ");
    source.append(std::to_string(round + 1U)).append(";\n");
    source.append("transposed_").append(suffix).append(" = scaled_").append(suffix).append(".';\n");
    source.append("selected_")
        .append(suffix)
        .append(" = empty_")
        .append(suffix)
        .append("([], :);\n");
    source += "grown_" + suffix + " = [];\n";
    source += "grown_" + suffix + "(1, 32) = " + std::to_string(round + 1U) + ";\n";
  }
  const auto last = std::to_string(rounds - 1U);
  source += "disp(length(transposed_" + last + ") + length(selected_" + last + ") + grown_" + last +
            "(1, 32))\n";
  return source;
}

std::string matlab_complex_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "values = [";
  for (std::size_t index = 0; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += std::to_string(index + 1U) + "+" + std::to_string(index % 7U + 1U) + "i";
  }
  source += "];\noffsets = [";
  for (std::size_t index = 0; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += std::to_string(index % 5U + 1U) + "i";
  }
  source += "];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "values = (values + offsets) .* (1-2i);\n";
    source += "conjugated = conj(values(1));\n";
    source += "hermitian = values';\n";
  }
  source +=
      "dynamic = scale_complex_elements(values, 2i);\n"
      "disp(real(dynamic(1)) + imag(conjugated) + real(hermitian(1,1)))\n"
      "function result = scale_complex_elements(input, factor)\n"
      "result = input .* factor;\n"
      "end\n";
  return source;
}

std::string matlab_complex_matrix_workload(const std::size_t rounds) {
  std::string source =
      "hermitian = [4 2i; -2i 5];\n"
      "dense = [1i 0; 0 -1i];\n"
      "column = [6+8i; 12-7i];\n"
      "row = [2 8-3i];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "product = hermitian * dense;\n";
    source += "hermitian_solution = hermitian \\ column;\n";
    source += "dense_solution = dense \\ column;\n";
    source += "right_solution = row / hermitian;\n";
    source += "powered = hermitian ^ 3;\n";
    source += "inverse = dense ^ -1;\n";
  }
  source +=
      "disp(real(product(1, 1)) + real(hermitian_solution(1)) + "
      "real(dense_solution(1)) + real(right_solution(1)) + "
      "real(powered(1, 1)) + real(inverse(1, 1)))\n";
  return source;
}

std::string matlab_complex_rectangular_solve_workload(const std::size_t rounds) {
  std::string source =
      "tall = [1+1i 2-1i; 2 -1+1i; 1-1i 1+2i; 3+1i 2];\n"
      "tall_rhs = [8+4i -2+5i; 7-1i 4+2i; 3-5i 8+1i; 9+2i 1-3i];\n"
      "wide = [1+1i 0 1-1i 2; 0 1-1i 2+1i -1i];\n"
      "wide_rhs = [3+1i 5-2i; 2-4i 1+3i];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "least_squares = tall \\ tall_rhs;\n";
    source += "basic_underdetermined = wide \\ wide_rhs;\n";
    source += "right_overdetermined = [3+1i 2-4i 5+2i -1i] / wide;\n";
    source += "right_underdetermined = [7+1i 3+4i] / tall;\n";
  }
  source +=
      "disp(real(least_squares(1, 1)) + real(basic_underdetermined(1, 1)) + "
      "real(right_overdetermined(1)) + real(right_underdetermined(1)))\n";
  return source;
}

std::string matlab_matrix_solve_workload(const std::size_t rounds) {
  std::string source =
      "coefficient = [4 1 0 0; 2 5 1 0; 0 1 6 2; 0 0 2 7];\n"
      "right_hand_side = [9; 8; 7; 6];\n"
      "tall = [1 0 0; 0 1 0; 0 0 1; 1 1 0; 0 1 1];\n"
      "tall_rhs = [1 2; 2 3; 3 4; 4 5; 5 6];\n"
      "wide = [1 0 0 1 0; 0 1 0 0 1; 0 0 1 1 1];\n"
      "wide_rhs = [1 2; 2 3; 3 4];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "solution = coefficient \\ right_hand_side;\n";
    source += "quotient = [1 2 3 4] / coefficient;\n";
    source += "powered = coefficient ^ 3;\n";
    source += "least_squares = tall \\ tall_rhs;\n";
    source += "basic_underdetermined = wide \\ wide_rhs;\n";
    source += "rectangular_right = [1 2 3] / tall;\n";
  }
  source +=
      "disp(solution(1) + quotient(4) + powered(1, 1) + "
      "least_squares(1, 1) + basic_underdetermined(1, 1) + rectangular_right(1))\n";
  return source;
}

std::string matlab_sparse_solve_workload(const std::size_t rounds) {
  std::string source =
      "tridiagonal = sparse([2 1 0 0; 0 3 1 0; 0 0 4 1; 0 0 0 5]);\n"
      "pivoted = sparse([0 2 0 0; 1 3 1 0; 0 1 4 1; 0 0 1 5]);\n"
      "dense_rhs = [4; 9; 19; 20];\n"
      "sparse_rhs = sparse([4; 10; 18; 17]);\n"
      "left = sparse([7 8 14 3; 16 23 29 9]);\n"
      "right = sparse([1 2 0 0; 0 3 1 0; 2 0 4 1; 0 1 0 5]);\n"
      "constructed = sparse([1 4 1 2], [1 1 1 3], [2 5 -2 7], 4, 4, 8);\n"
      "inferred = sparse([2 1 2], [4 2 4], [3 6 4]);\n"
      "zero_matrix = sparse(4, 4);\n"
      "empty_coefficient = sparse([], [], []);\n"
      "empty_dense_rhs = reshape([], 0, 3);\n"
      "empty_sparse_rhs = sparse(0, 3);\n"
      "empty_dense_left = reshape([], 2, 0);\n"
      "empty_sparse_left = sparse(2, 0);\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "tridiagonal_solution = tridiagonal \\ dense_rhs;\n";
    source += "pivoted_solution = pivoted \\ sparse_rhs;\n";
    source += "quotient = left / right;\n";
    source += "dense_pivoted = full(pivoted_solution);\n";
    source += "dense_quotient = full(quotient);\n";
    source += "transposed = constructed.';\n";
    source += "conjugate_transposed = inferred';\n";
    source += "dense_transposed = full(transposed);\n";
    source += "empty_dense_solution = empty_coefficient \\ empty_dense_rhs;\n";
    source += "empty_sparse_solution = empty_coefficient \\ empty_sparse_rhs;\n";
    source += "empty_dense_quotient = empty_dense_left / empty_coefficient;\n";
    source += "empty_sparse_quotient = empty_sparse_left / empty_coefficient;\n";
  }
  source +=
      "disp(tridiagonal_solution(1) + dense_pivoted(1) + dense_quotient(1, 1) + "
      "nnz(pivoted) + nnz(zero_matrix) + nnz(conjugate_transposed) + "
      "dense_transposed(1, 4) + length(empty_dense_solution) + "
      "length(full(empty_sparse_solution)) + length(empty_dense_quotient) + "
      "length(full(empty_sparse_quotient)))\n";
  return source;
}

std::string matlab_sparse_multiply_workload(const std::size_t width, const std::size_t rounds) {
  const auto append_matrix = [&](std::string& source, const std::string_view name,
                                 const std::size_t row_factor, const std::size_t column_factor,
                                 const std::size_t modulus) {
    source.append(name).append(" = sparse([");
    for (std::size_t row = 0U; row < width; ++row) {
      if (row != 0U) source += "; ";
      for (std::size_t column = 0U; column < width; ++column) {
        if (column != 0U) source += ' ';
        source += row == column || (row_factor * row + column_factor * column) % modulus == 0U
                      ? std::to_string(row + column + 1U)
                      : "0";
      }
    }
    source += "]);\n";
  };
  std::string source;
  append_matrix(source, "left", 1U, 3U, 13U);
  append_matrix(source, "right", 2U, 1U, 17U);
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "sparse_product = left * right;\n";
    source += "sparse_dense_product = left * full(right);\n";
    source += "dense_sparse_product = full(left) * right;\n";
  }
  source +=
      "disp(nnz(sparse_product) + sparse_dense_product(1, 1) + "
      "dense_sparse_product(1, 1))\n";
  return source;
}

std::string matlab_complex_sparse_multiply_workload(const std::size_t width,
                                                    const std::size_t rounds) {
  const auto append_matrix = [&](std::string& source, const std::string_view name,
                                 const std::size_t row_factor, const std::size_t column_factor,
                                 const std::size_t modulus, const bool complex) {
    source.append(name).append(" = sparse([");
    for (std::size_t row = 0U; row < width; ++row) {
      if (row != 0U) source += "; ";
      for (std::size_t column = 0U; column < width; ++column) {
        if (column != 0U) source += ' ';
        if (row != column && (row_factor * row + column_factor * column) % modulus != 0U) {
          source += '0';
          continue;
        }
        source += std::to_string(row + column + 1U);
        if (complex) {
          source += '+';
          source += std::to_string((3U * row + 5U * column) % 11U + 1U);
          source += 'i';
        }
      }
    }
    source += "]);\n";
  };
  std::string source;
  append_matrix(source, "complex_left", 1U, 3U, 13U, true);
  append_matrix(source, "complex_right", 2U, 1U, 17U, true);
  append_matrix(source, "real_left", 1U, 3U, 13U, false);
  append_matrix(source, "real_right", 2U, 1U, 17U, false);
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "complex_product = complex_left * complex_right;\n";
    source += "complex_real_product = complex_left * real_right;\n";
    source += "real_complex_product = real_left * complex_right;\n";
    source += "sparse_dense_product = complex_left * full(complex_right);\n";
    source += "dense_sparse_product = full(complex_left) * complex_right;\n";
  }
  source +=
      "disp(nnz(complex_product) + nnz(complex_real_product) + "
      "nnz(real_complex_product) + real(sparse_dense_product(1, 1)) + "
      "imag(dense_sparse_product(1, 1)))\n";
  return source;
}

std::string matlab_sparse_scale_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = sparse([";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += (row == column || (5U * row + 3U * column) % 19U == 0U)
                    ? std::to_string(row + column + 1U)
                    : "0";
    }
  }
  source += "]);\n";
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "right_scaled = matrix * 3;\n";
    source += "left_scaled = -2 * matrix;\n";
    source += "zero_scaled = matrix * 0;\n";
    source += "matrix = right_scaled * 0.5;\n";
  }
  source +=
      "disp(nnz(matrix) + nnz(left_scaled) + nnz(zero_scaled) + "
      "right_scaled(1, 1))\n";
  return source;
}

std::string matlab_sparse_elementwise_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = sparse([";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += (row == column || (7U * row + 5U * column) % 23U == 0U)
                    ? std::to_string(row + column + 1U)
                    : "0";
    }
  }
  source += "]);\ndense = [";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += std::to_string((3U * row + 2U * column) % 29U + 1U);
    }
  }
  source += "];\nrow = sparse([";
  for (std::size_t index = 0U; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += "1";
  }
  source += "], [";
  for (std::size_t index = 0U; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += std::to_string(index + 1U);
  }
  source += "], [";
  for (std::size_t index = 0U; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += index % 4U == 0U ? std::to_string(index + 1U) : "0";
  }
  source += "], 1, " + std::to_string(width) + ");\ncolumn = row.';\n";
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "right_scalar = matrix .* 3;\n";
    source += "left_scalar = -2 .* matrix;\n";
    source += "sparse_dense = row .* dense;\n";
    source += "dense_sparse = dense .* column;\n";
    source += "sparse_sparse = row .* column;\n";
    source += "matrix = matrix .* matrix;\n";
  }
  source +=
      "disp(nnz(right_scalar) + nnz(left_scalar) + nnz(sparse_dense) + "
      "nnz(dense_sparse) + nnz(sparse_sparse) + nnz(matrix))\n";
  return source;
}

std::string matlab_sparse_arithmetic_workload(const std::size_t width, const std::size_t rounds) {
  const auto append_matrix = [&](std::string& source, const std::string_view name,
                                 const std::size_t row_factor, const std::size_t column_factor,
                                 const std::size_t modulus) {
    source.append(name).append(" = sparse([");
    for (std::size_t row = 0U; row < width; ++row) {
      if (row != 0U) source += "; ";
      for (std::size_t column = 0U; column < width; ++column) {
        if (column != 0U) source += ' ';
        source += row == column || (row_factor * row + column_factor * column) % modulus == 0U
                      ? std::to_string(row + column + 1U)
                      : "0";
      }
    }
    source += "]);\n";
  };
  std::string source;
  append_matrix(source, "left", 3U, 5U, 19U);
  append_matrix(source, "right", 7U, 2U, 23U);
  source += "dense = [";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += std::to_string((5U * row + 3U * column) % 31U + 1U);
    }
  }
  source += "];\nrow = sparse([";
  for (std::size_t index = 0U; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += "1";
  }
  source += "], [";
  for (std::size_t index = 0U; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += std::to_string(index + 1U);
  }
  source += "], [";
  for (std::size_t index = 0U; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += index % 3U == 0U ? std::to_string(index + 1U) : "1";
  }
  source += "], 1, " + std::to_string(width) + ");\ncolumn = row.';\n";
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "sparse_sum = left + right;\n";
    source += "sparse_difference = left - right;\n";
    source += "sparse_dense = left + dense;\n";
    source += "dense_sparse = dense - right;\n";
    source += "right_scalar = left + 3;\n";
    source += "left_scalar = 4 - right;\n";
    source += "row_broadcast = row + right;\n";
    source += "column_broadcast = left - column;\n";
    source += "outer_broadcast = row + column;\n";
    source += "left = sparse_sum;\n";
    source += "right = sparse_difference;\n";
  }
  source +=
      "disp(nnz(left) + nnz(right) + nnz(row_broadcast) + nnz(column_broadcast) + "
      "nnz(outer_broadcast) + sparse_dense(1, 1) + dense_sparse(1, 1) + "
      "right_scalar(1, 1) + left_scalar(1, 1))\n";
  return source;
}

std::string matlab_complex_sparse_arithmetic_workload(const std::size_t width,
                                                      const std::size_t rounds) {
  const auto append_matrix = [&](std::string& source, const std::string_view name,
                                 const std::size_t factor, const std::size_t modulus) {
    source.append(name).append(" = sparse([");
    for (std::size_t row = 0U; row < width; ++row) {
      if (row != 0U) source += "; ";
      for (std::size_t column = 0U; column < width; ++column) {
        if (column != 0U) source += ' ';
        if (row == column || (factor * row + column) % modulus == 0U) {
          source += std::to_string(row + column + 1U) + "+" +
                    std::to_string((row + 2U * column) % 17U + 1U) + "i";
        } else {
          source += "0";
        }
      }
    }
    source += "]);\n";
  };
  std::string source;
  append_matrix(source, "left", 3U, 19U);
  append_matrix(source, "right", 7U, 23U);
  source += "dense = [";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += std::to_string((5U * row + 3U * column) % 31U + 1U);
    }
  }
  source += "];\nrow = sparse([1 1], [1 " + std::to_string(width) + "], [1+2i 3-4i], 1, " +
            std::to_string(width) + ");\ncolumn = row.';\n";
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "sparse_sum = left + right;\n";
    source += "sparse_difference = left - right;\n";
    source += "mixed_right = left + dense;\n";
    source += "mixed_left = dense - right;\n";
    source += "scalar_right = left + (2-3i);\n";
    source += "scalar_left = (4+5i) - right;\n";
    source += "broadcast = row + column;\n";
    source += "left = sparse_sum;\n";
    source += "right = sparse_difference;\n";
  }
  source +=
      "disp(nnz(left) + nnz(right) + nnz(broadcast) + real(mixed_right(1, 1)) + "
      "imag(mixed_left(1, 1)) + real(scalar_right(1, 1)) + imag(scalar_left(1, 1)))\n";
  return source;
}

std::string matlab_sparse_power_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "base = sparse([";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += row == column ? "1" : (column == row + 1U ? "1" : "0");
    }
  }
  source += "]);\nlogical_base = sparse([";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += row == column ? "true" : "false";
    }
  }
  source += "]);\n";
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "squared = base ^ 2;\n";
    source += "cubed = base ^ 3;\n";
    source += "identity = base ^ 0;\n";
    source += "logical_power = logical_base ^ 2;\n";
    source += "base = squared;\n";
  }
  source += "disp(nnz(squared) + nnz(cubed) + nnz(identity) + nnz(logical_power))\n";
  return source;
}

std::string matlab_logical_sparse_workload(const std::size_t width, const std::size_t rounds) {
  const auto width_text = std::to_string(width);
  const auto element_count = std::to_string(width * width);
  std::string source;
  source.reserve(width * width * 6U + width * 48U + rounds * 192U + 256U);
  source = "dense = [";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += row == column || (5U * row + 3U * column) % 19U == 0U ? "true" : "false";
    }
  }
  source += "];\nmatrix = sparse(dense);\ntriplets = sparse([";
  for (std::size_t index = 0U; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += std::to_string(index + 1U) + " " + std::to_string(index + 1U);
  }
  source += "], [";
  for (std::size_t index = 0U; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += std::to_string(index + 1U) + " " + std::to_string(index + 1U);
  }
  source += "], [";
  for (std::size_t index = 0U; index < width; ++index) {
    if (index != 0U) source += ' ';
    source += index % 2U == 0U ? "false true" : "true false";
  }
  source.append("], ").append(width_text).append(", ").append(width_text).append(");\n");
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "transposed = matrix.';\n";
    source.append("selected = triplets([")
        .append(width_text)
        .append(" 1], [")
        .append(width_text)
        .append(" 1]);\n");
    source += "reshaped = reshape(selected, [1 4]);\n";
    source.append("triplets(1, ").append(std::to_string(round % width + 1U)).append(") = true;\n");
    source += "dense_result = full(reshaped);\n";
  }
  source.append("disp(nnz(matrix) + nnz(triplets) + nnz(transposed) + nnz(dense_result) + ")
      .append(element_count)
      .append(")\n");
  return source;
}

std::string matlab_sparse_logical_workload(const std::size_t width, const std::size_t rounds) {
  const auto append_logical_matrix =
      [&](std::string& source, const std::string_view name, const std::size_t row_factor,
          const std::size_t column_factor, const std::size_t modulus) {
        source.append(name).append(" = [");
        for (std::size_t row = 0U; row < width; ++row) {
          if (row != 0U) source += "; ";
          for (std::size_t column = 0U; column < width; ++column) {
            if (column != 0U) source += ' ';
            source += row == column || (row_factor * row + column_factor * column) % modulus == 0U
                          ? "true"
                          : "false";
          }
        }
        source += "];\n";
      };
  std::string source;
  source.reserve(width * width * 14U + rounds * 320U + 512U);
  append_logical_matrix(source, "left_dense", 5U, 3U, 19U);
  append_logical_matrix(source, "right_dense", 7U, 2U, 23U);
  source +=
      "left = sparse(left_dense);\n"
      "right = sparse(right_dense);\n"
      "row = sparse([1 1 1], [1 2 " +
      std::to_string(width) + "], [true false true], 1, " + std::to_string(width) +
      ");\n"
      "column = row.';\n";
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "negated = ~left;\n";
    source += "sparse_and = left & right;\n";
    source += "sparse_dense_and = left & right_dense;\n";
    source += "dense_sparse_and = left_dense & right;\n";
    source += "sparse_or = left | right;\n";
    source += "sparse_dense_or = left | right_dense;\n";
    source += "broadcast_and = row & column;\n";
    source += "broadcast_or = row | column;\n";
    source += "scalar_and = row & true;\n";
    source += "scalar_or = row | false;\n";
    source += "left = sparse_and;\n";
    source += "right = sparse_or;\n";
  }
  source +=
      "disp(nnz(negated) + nnz(sparse_dense_and) + nnz(dense_sparse_and) + "
      "nnz(sparse_dense_or) + nnz(broadcast_and) + nnz(broadcast_or) + "
      "nnz(scalar_and) + nnz(scalar_or))\n";
  return source;
}

std::string matlab_sparse_reduction_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = sparse([";
  source.reserve(width * width * 6U + rounds * 256U + 512U);
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += row == column || (row + 3U * column) % 17U == 0U ? std::to_string(row + column + 1U)
                                                                 : "0";
    }
  }
  source += "]);\n";
  source += "logical_values = sparse([";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += row == column || (5U * row + column) % 19U == 0U ? "true" : "false";
    }
  }
  source += "]);\n";
  source += "zero_rows = sparse([], [], [], 0, " + std::to_string(width) + ");\n";
  source += "zero_columns = sparse([], [], [], " + std::to_string(width) + ", 0);\n";
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "column_all = all(logical_values);\n";
    source += "column_any = any(matrix);\n";
    source += "row_all = all(logical_values, 2);\n";
    source += "row_any = any(matrix, 2);\n";
    source += "total_all = all(matrix, 'all');\n";
    source += "total_any = any(matrix, 'all');\n";
    source += "zero_rows_all = all(zero_rows);\n";
    source += "zero_columns_any = any(zero_columns, 2);\n";
    source += "unchanged = any(matrix, 3);\n";
  }
  source +=
      "disp(nnz(column_all) + nnz(column_any) + nnz(row_all) + nnz(row_any) + "
      "total_all + total_any + nnz(zero_rows_all) + nnz(zero_columns_any) + "
      "nnz(unchanged))\n";
  return source;
}

std::string matlab_sparse_index_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = sparse([";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += (row == column || (row + 2U * column) % 11U == 0U)
                    ? std::to_string(row + column + 1U)
                    : "0";
    }
  }
  source += "]);\nrow_mask = [";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += ' ';
    source += row % 3U == 0U ? "true" : "false";
  }
  source += "];\n";
  const auto last = std::to_string(width);
  const auto linear_last = std::to_string(width * width);
  for (std::size_t round = 0U; round < rounds; ++round) {
    source.append("scalar = matrix(").append(linear_last).append(");\n");
    source.append("linear = matrix([")
        .append(linear_last)
        .append(" 1; ")
        .append(last)
        .append(" 2]);\n");
    source += "logical_rows = matrix(row_mask, :);\n";
    source.append("repeated = matrix([")
        .append(last)
        .append(" 1 ")
        .append(last)
        .append("], [")
        .append(last)
        .append(" 1 2]);\n");
    source.append("strided = matrix(")
        .append(last)
        .append(":-2:1, ")
        .append(last)
        .append(":-3:1);\n");
  }
  source += "disp(scalar + nnz(linear) + nnz(logical_rows) + nnz(repeated) + nnz(strided))\n";
  return source;
}

std::string matlab_sparse_reshape_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = sparse([";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += (row == column || (3U * row + column) % 17U == 0U)
                    ? std::to_string(row + column + 1U)
                    : "0";
    }
  }
  source += "]);\n";
  const auto half = std::to_string(width / 2U);
  const auto twice = std::to_string(width * 2U);
  const auto quarter = std::to_string(width / 4U);
  const auto width_text = std::to_string(width);
  for (std::size_t round = 0U; round < rounds; ++round) {
    source.append("wide = reshape(matrix, ").append(half).append(", ").append(twice).append(");\n");
    source.append("matrix = reshape(wide, [], ").append(width_text).append(");\n");
    source.append("folded = reshape(matrix, ")
        .append(quarter)
        .append(", 4, ")
        .append(width_text)
        .append(");\n");
    source.append("matrix = reshape(folded, [")
        .append(width_text)
        .append(" ")
        .append(width_text)
        .append("]);\n");
  }
  source += "disp(nnz(matrix) + nnz(wide) + nnz(folded))\n";
  return source;
}

std::string matlab_sparse_assignment_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = sparse([";
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      source += row == column ? std::to_string(row + 1U) : "0";
    }
  }
  source += "]);\nblock = sparse([1 0; 0 2]);\n";
  for (std::size_t round = 0U; round < rounds; ++round) {
    const auto value = std::to_string(round + 1U);
    source.append("matrix([1 3 1]) = [")
        .append(value)
        .append(" ")
        .append(std::to_string(round + 2U))
        .append(" ")
        .append(std::to_string(round + 3U))
        .append("];\n");
    source.append("matrix(2:3, [2 4]) = [0 ")
        .append(value)
        .append("; ")
        .append(std::to_string(round + 4U))
        .append(" 0];\n");
    source += "matrix(5:6, 5:6) = block;\n";
    source += "matrix(7, 7) = 0;\n";
  }
  const auto grown = std::to_string(width + 1U);
  source.append("matrix(").append(grown).append(", ").append(grown).append(") = 17;\n");
  source += "matrix(:, 2) = [];\n";
  source += "disp(nnz(matrix) + issparse(matrix))\n";
  return source;
}

std::string matlab_complex_sparse_workload(const std::size_t width, const std::size_t rounds) {
  std::string source = "matrix = sparse([";
  source.reserve(width * width * 8U + rounds * 320U + 1024U);
  for (std::size_t row = 0U; row < width; ++row) {
    if (row != 0U) source += "; ";
    for (std::size_t column = 0U; column < width; ++column) {
      if (column != 0U) source += ' ';
      if (row == column) {
        source.append(std::to_string(row + 1U))
            .append("+")
            .append(std::to_string(column + 2U))
            .append("i");
      } else {
        source += "0";
      }
    }
  }
  source += "]);\n";
  source += "triplets = sparse([1 1 2 3], [1 1 2 3], [1+2i 3-2i 4i 5-6i], ";
  source.append(std::to_string(width)).append(", ").append(std::to_string(width)).append(");\n");
  const auto width_text = std::to_string(width);
  for (std::size_t round = 0U; round < rounds; ++round) {
    source += "plain = matrix.';\n";
    source += "hermitian = matrix';\n";
    source.append("selected = matrix([")
        .append(width_text)
        .append(" 1 2 3], [")
        .append(width_text)
        .append(" 1 2 3]);\n");
    source += "reshaped = reshape(selected, [1 16]);\n";
    source.append("matrix(1, 2) = ")
        .append(std::to_string(round + 1U))
        .append("-")
        .append(std::to_string(round + 2U))
        .append("i;\n");
    source += "matrix(2, 2) = 0;\n";
    source += "copied = sparse(matrix);\n";
  }
  source += "materialized = full(matrix);\n";
  source +=
      "disp(nnz(copied) + nnz(triplets) + nnz(plain) + nnz(hermitian) + nnz(reshaped) + "
      "real(materialized(1, 2)) + imag(materialized(1, 2)))\n";
  return source;
}

std::string matlab_rank_aware_solve_workload(const std::size_t rounds) {
  std::string source =
      "rank_deficient_tall = [1 2; 2 4; 3 6; 4 8];\n"
      "rank_deficient_rhs = [1 2; 2 4; 3 6; 4 8];\n"
      "wide = [1 0 1 2; 0 1 1 1];\n"
      "wide_rhs = [2 4; 3 6];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "ranked_basic = rank_deficient_tall \\ rank_deficient_rhs;\n";
    source += "wide_basic = wide \\ wide_rhs;\n";
    source += "ranked_right = [1 2] / rank_deficient_tall;\n";
  }
  source += "disp(ranked_basic(2, 1) + wide_basic(3, 2) + ranked_right(1))\n";
  return source;
}

std::string matlab_conditioned_square_solve_workload(const std::size_t rounds) {
  std::string source =
      "singular = [1 0; 0 0];\n"
      "nearly_singular = [16 2 3 13; 5 11 10 8; 9 7 6 12; 4 14 15 1];\n"
      "small_rhs = [1; 1];\n"
      "large_rhs = [34; 34; 34; 34];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "singular_left = singular \\ small_rhs;\n";
    source += "singular_right = [1 1] / singular;\n";
    source += "conditioned = nearly_singular \\ large_rhs;\n";
  }
  source += "disp(singular_left(1) + singular_right(1) + conditioned(1))\n";
  return source;
}

std::string matlab_structured_square_solve_workload(const std::size_t rounds) {
  std::string source =
      "diagonal = [4 0 0 0; 0 5 0 0; 0 0 6 0; 0 0 0 7.5];\n"
      "lower = [4 0 0 0; 1 5 0 0; 2 1 6 0; 3 2 1 7.5];\n"
      "upper = [4 1 2 3; 0 5 1 2; 0 0 6 1; 0 0 0 7.5];\n"
      "dense = [8 1 2 3; 1 9 3 2; 2 3 10 1; 3 2 1 11.5];\n"
      "right_hand_side = [4; 6; 8; 10];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "diagonal_left = diagonal \\ right_hand_side;\n";
    source += "lower_left = lower \\ right_hand_side;\n";
    source += "upper_left = upper \\ right_hand_side;\n";
    source += "dense_left = dense \\ right_hand_side;\n";
    source += "upper_right = [1 2 3 4] / upper;\n";
  }
  source +=
      "disp(diagonal_left(1) + lower_left(2) + upper_left(3) + dense_left(4) + "
      "upper_right(1))\n";
  return source;
}

std::string matlab_advanced_structured_square_solve_workload(const std::size_t rounds) {
  std::string source =
      "tridiagonal = [0 2 0 0; 1 3 4 0; 0 5 6 7; 0 0 8 9];\n"
      "positive_definite = [8 1 2 3; 1 9 3 2; 2 3 10 1; 3 2 1 11.5];\n"
      "symmetric_indefinite = [0 1 2 3; 1 0 4 5; 2 4 0 6; 3 5 6 0];\n"
      "right_hand_side = [4; 6; 8; 10];\n";
  for (std::size_t round = 0; round < rounds; ++round) {
    source += "tridiagonal_left = tridiagonal \\ right_hand_side;\n";
    source += "tridiagonal_right = [1 2 3 4] / tridiagonal;\n";
    source += "positive_definite_left = positive_definite \\ right_hand_side;\n";
    source += "positive_definite_right = [1 2 3 4] / positive_definite;\n";
    source += "indefinite_left = symmetric_indefinite \\ right_hand_side;\n";
  }
  source +=
      "disp(tridiagonal_left(1) + tridiagonal_right(2) + positive_definite_left(3) + "
      "positive_definite_right(4) + indefinite_left(1))\n";
  return source;
}

std::string source_extension(const mpf::SourceLanguage language) {
  switch (language) {
    case mpf::SourceLanguage::python: return ".py";
    case mpf::SourceLanguage::matlab: return ".m";
    case mpf::SourceLanguage::fortran: return ".f90";
    case mpf::SourceLanguage::typescript: return ".ts";
    case mpf::SourceLanguage::automatic: return ".txt";
  }
  return ".txt";
}

mpf::TranspileResult compile(const Scenario& scenario, const mpf::TargetLanguage target) {
  mpf::TranspileOptions options;
  options.language = scenario.language;
  options.target = target;
  options.filename = scenario.name + source_extension(scenario.language);
  options.emit_source_banner = false;
  return mpf::Transpiler{}.transpile(scenario.source, options);
}

bool measure(const Scenario& scenario, Measurement& measurement) {
  std::vector<std::size_t> samples;
  std::size_t peak_arena = 0;
  std::size_t generated = 0;
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto reference = compile(scenario, target);
    if (!reference.success()) {
      std::cerr << scenario.name << ": " << mpf::to_string(target) << " transpilation failed";
      for (const auto& diagnostic : reference.diagnostics) {
        std::cerr << " [" << diagnostic.code << "] " << diagnostic.message;
      }
      std::cerr << '\n';
      return false;
    }
    if (reference.report.mir_memory_dependences.total < scenario.minimum_memory_dependences ||
        (scenario.require_loop_carried_dependence &&
         reference.report.mir_memory_dependences.loop_carried == 0U)) {
      std::cerr << scenario.name << ": MIR memory-dependence gate failed\n";
      return false;
    }
    for (std::size_t sample = 0; sample < 3; ++sample) {
      const auto started = Clock::now();
      const auto result = compile(scenario, target);
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started);
      if (!result.success() || result.code != reference.code ||
          result.source_map.to_json() != reference.source_map.to_json() ||
          result.dependencies != reference.dependencies) {
        return false;
      }
      samples.push_back(static_cast<std::size_t>(elapsed.count()));
      peak_arena = std::max(peak_arena, result.report.peak_arena_bytes);
      generated = std::max(generated, result.code.size());
    }
  }
  std::sort(samples.begin(), samples.end());
  measurement.name = scenario.name;
  measurement.latency_nanoseconds = samples[samples.size() / 2U];
  measurement.throughput_bytes_per_second =
      measurement.latency_nanoseconds == 0
          ? 0
          : scenario.source.size() * 1'000'000'000U / measurement.latency_nanoseconds;
  measurement.peak_arena_bytes = peak_arena;
  measurement.generated_bytes = generated;
  return true;
}

bool concurrent_gate(const Scenario& scenario) {
  std::vector<std::future<mpf::TranspileResult>> work;
  work.reserve(8);
  for (std::size_t index = 0; index < 8; ++index) {
    work.push_back(std::async(std::launch::async, [&, index] {
      return compile(scenario,
                     index % 2U == 0 ? mpf::TargetLanguage::javascript : mpf::TargetLanguage::cpp);
    }));
  }
  for (auto& future : work) {
    if (!future.get().success()) return false;
  }
  return true;
}

}  // namespace

int main() {
  const std::vector<Scenario> scenarios{
      {"small", "value = 40 + 2\nprint(value)\n"},
      {"throughput", assignment_workload(600)},
      {"cfg", control_flow_workload(80)},
      {"shape", shape_workload(16)},
      {"function-graph", function_workload(40)},
      {"typescript-throughput", typescript_workload(300), mpf::SourceLanguage::typescript},
      {"storage-regions", storage_region_workload(128), mpf::SourceLanguage::fortran},
      {"memory-dependence", memory_dependence_workload(16), mpf::SourceLanguage::python, 32U, true},
      {"matlab-array-kernel", matlab_array_workload(24, 24), mpf::SourceLanguage::matlab},
      {"matlab-tensor-kernel", matlab_tensor_workload(24), mpf::SourceLanguage::matlab},
      {"matlab-logical-kernel", matlab_logical_workload(24, 24), mpf::SourceLanguage::matlab},
      {"matlab-logical-reduction", matlab_logical_reduction_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-dynamic-end", matlab_dynamic_end_workload(32), mpf::SourceLanguage::matlab},
      {"matlab-dynamic-broadcast", matlab_dynamic_broadcast_workload(32),
       mpf::SourceLanguage::matlab},
      {"matlab-shape-mutation", matlab_shape_mutation_workload(32), mpf::SourceLanguage::matlab},
      {"matlab-empty-arrays", matlab_empty_array_workload(24), mpf::SourceLanguage::matlab},
      {"matlab-complex-kernel", matlab_complex_workload(24, 24), mpf::SourceLanguage::matlab},
      {"matlab-complex-matrix-kernel", matlab_complex_matrix_workload(24),
       mpf::SourceLanguage::matlab},
      {"matlab-complex-rectangular-solve", matlab_complex_rectangular_solve_workload(24),
       mpf::SourceLanguage::matlab},
      {"matlab-matrix-solve", matlab_matrix_solve_workload(24), mpf::SourceLanguage::matlab},
      {"matlab-sparse-solve", matlab_sparse_solve_workload(24), mpf::SourceLanguage::matlab},
      {"matlab-sparse-multiply", matlab_sparse_multiply_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-complex-sparse-multiply", matlab_complex_sparse_multiply_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-sparse-scale", matlab_sparse_scale_workload(24, 24), mpf::SourceLanguage::matlab},
      {"matlab-sparse-elementwise", matlab_sparse_elementwise_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-sparse-arithmetic", matlab_sparse_arithmetic_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-complex-sparse-arithmetic", matlab_complex_sparse_arithmetic_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-sparse-power", matlab_sparse_power_workload(24, 24), mpf::SourceLanguage::matlab},
      {"matlab-logical-sparse", matlab_logical_sparse_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-sparse-logical", matlab_sparse_logical_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-sparse-reduction", matlab_sparse_reduction_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-sparse-index", matlab_sparse_index_workload(24, 24), mpf::SourceLanguage::matlab},
      {"matlab-sparse-reshape", matlab_sparse_reshape_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-sparse-assignment", matlab_sparse_assignment_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-complex-sparse", matlab_complex_sparse_workload(24, 24),
       mpf::SourceLanguage::matlab},
      {"matlab-rank-aware-solve", matlab_rank_aware_solve_workload(24),
       mpf::SourceLanguage::matlab},
      {"matlab-conditioned-square-solve", matlab_conditioned_square_solve_workload(24),
       mpf::SourceLanguage::matlab},
      {"matlab-structured-square-solve", matlab_structured_square_solve_workload(24),
       mpf::SourceLanguage::matlab},
      {"matlab-advanced-structured-square-solve",
       matlab_advanced_structured_square_solve_workload(24), mpf::SourceLanguage::matlab}};
  std::vector<Measurement> measurements;
  for (const auto& scenario : scenarios) {
    Measurement measurement;
    if (!measure(scenario, measurement)) {
      std::cerr << "performance scenario failed: " << scenario.name << '\n';
      return 1;
    }
    measurements.push_back(std::move(measurement));
  }
  if (!concurrent_gate(scenarios.front())) {
    std::cerr << "parallel compilation gate failed\n";
    return 1;
  }

  std::size_t max_latency = 0;
  std::size_t min_throughput = static_cast<std::size_t>(-1);
  std::size_t max_arena = 0;
  std::size_t max_generated = 0;
  std::size_t matlab_max_latency = 0;
  std::size_t matlab_min_throughput = static_cast<std::size_t>(-1);
  std::size_t matlab_max_generated = 0;
  for (const auto& measurement : measurements) {
    max_latency = std::max(max_latency, measurement.latency_nanoseconds);
    min_throughput = std::min(min_throughput, measurement.throughput_bytes_per_second);
    max_arena = std::max(max_arena, measurement.peak_arena_bytes);
    max_generated = std::max(max_generated, measurement.generated_bytes);
    if (measurement.name.rfind("matlab-", 0U) == 0U) {
      matlab_max_latency = std::max(matlab_max_latency, measurement.latency_nanoseconds);
      matlab_min_throughput =
          std::min(matlab_min_throughput, measurement.throughput_bytes_per_second);
      matlab_max_generated = std::max(matlab_max_generated, measurement.generated_bytes);
    }
  }

  std::cout << "{\"schemaVersion\":3,\"projectVersion\":\"" << MPF_VERSION_STRING
            << "\",\"maxLatencyNanoseconds\":" << max_latency
            << ",\"minThroughputBytesPerSecond\":" << min_throughput
            << ",\"maxPeakArenaBytes\":" << max_arena << ",\"maxGeneratedBytes\":" << max_generated
            << ",\"matlabMaxLatencyNanoseconds\":" << matlab_max_latency
            << ",\"matlabMinThroughputBytesPerSecond\":" << matlab_min_throughput
            << ",\"matlabMaxGeneratedBytes\":" << matlab_max_generated
            << ",\"parallelCompilations\":8,\"scenarios\":[";
  for (std::size_t index = 0; index < measurements.size(); ++index) {
    if (index != 0) std::cout << ',';
    const auto& value = measurements[index];
    std::cout << "{\"name\":\"" << value.name
              << "\",\"latencyNanoseconds\":" << value.latency_nanoseconds
              << ",\"throughputBytesPerSecond\":" << value.throughput_bytes_per_second
              << ",\"peakArenaBytes\":" << value.peak_arena_bytes
              << ",\"generatedBytes\":" << value.generated_bytes << '}';
  }
  std::cout << "]}\n";
  return 0;
}
