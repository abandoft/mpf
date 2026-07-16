program statement_tokens
  implicit none
  integer(kind=8) :: total = 0, index
  integer :: block(2) = [20, 22]

  do index = 1, 2
    if (index .eq. 1) then
      total = total + block(index)
    else if (index .eq. 2) then
      total = total + block(index)
    else
      total = -1
    endif
  enddo

  write(*,*) total
end program statement_tokens
