program control_flow
  implicit none
  integer :: total = 0, index
  do index = 1, 10
    if (index .eq. 2) then
      cycle
    else if (index .eq. 5) then
      exit
    else
      total = total + index
    end if
  end do
  print *, total, index
end program control_flow
