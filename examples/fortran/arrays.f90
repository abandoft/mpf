program arrays
  implicit none
  integer :: values(3) = [1, 2, 3]
  values(2) = values(1) + 4
  values(3) = values(3) + 7
  print *, size(values), sum(values), values(2)
end program arrays
