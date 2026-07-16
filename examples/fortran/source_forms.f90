program source_forms
  implicit none
  integer :: values(4) = [1, &
    &2, 3, 4]
  integer :: total
  total = values(1) + &
    &values(2) + values(3) + values(4)
  total = total + 32; print *, total
end program source_forms
