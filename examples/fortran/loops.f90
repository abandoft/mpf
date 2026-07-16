program loops
  implicit none
  integer :: total = 0, index
  do index = 5, 1, -2
    total = total + index
  end do
  do while (total .lt. 12)
    total = total + 1
  end do
  print *, total, index
end program loops
