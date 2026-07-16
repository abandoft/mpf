program tensors
  implicit none
  integer :: cube(2, 2, 2) = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])

  cube(:, 1, 2) = [40, 2]
  print *, size(cube), sum(cube), sum(cube(:, 1, 2)), cube(1, 1, 2), cube(2, 1, 2)
end program tensors
