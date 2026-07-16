program sections
  implicit none
  integer :: matrix(2, 3) = reshape([1, 4, 2, 5, 3, 6], [2, 3])
  integer :: row(3)
  integer :: column(2)
  integer :: block(2, 2)
  integer :: reverse(3)

  row = matrix(2, :)
  column = matrix(:, 2)
  block = matrix(:, 1:2)
  reverse = matrix(2, 3:1:-1)
  print *, sum(row) + sum(column) + size(block) + reverse(1)
end program sections
