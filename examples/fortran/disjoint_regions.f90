program disjoint_regions
  integer :: values(6) = [1, 2, 3, 4, 5, 6]
  integer :: matrix(2, 4) = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 4])

  call update_vector(values(1:6:2), values(2:6:2))
  call update_matrix(matrix(:, 1:2), matrix(:, 3:4))
  print *, values(1), values(2), matrix(1, 1), matrix(1, 3)

contains

  subroutine update_vector(odd, even)
    integer, intent(inout) :: odd(:), even(:)
    odd(1) = 40
    even(1) = 2
  end subroutine update_vector

  subroutine update_matrix(first, last)
    integer, intent(inout) :: first(:, :), last(:, :)
    first(1, 1) = 41
    last(1, 1) = 1
  end subroutine update_matrix
end program disjoint_regions
