program assignments
  implicit none
  integer :: matrix(2,3) = reshape([1,4,2,5,3,6], [2,3])
  matrix(:,1:2) = reshape([1,3,2,4], [2,2])
  matrix(:,2) = [8,9]
  matrix(:,3) = 7
  matrix(1,:) = [4,5,6]
  print *, matrix(1,1) + matrix(2,2) + matrix(1,3)
end program assignments
