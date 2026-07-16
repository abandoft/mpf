program matrices
  implicit none
  integer :: matrix(2, 2) = reshape([1, 2, 3, 4], [2, 2])

  matrix(2, 1) = 7
  print *, size(matrix), sum(matrix), matrix(2, 1)
end program matrices
