program procedures
  implicit none
  print *, factorial(5), add_one(41), absolute(-42), combine(40, 2)
  call announce(42)
  call countdown(2)
contains
  recursive integer function factorial(n) result(value)
    integer, intent(in) :: n
    if (n <= 1) then
      value = 1
    else
      value = n * factorial(n - 1)
    end if
  end function factorial

  integer function add_one(n) result(value)
    integer, intent(in) :: n
    value = n + 1
  end function add_one

  integer function absolute(input) result(value)
    integer, intent(in) :: input
    if (input < 0) then
      value = -input
      return
    end if
    value = input
  end function absolute

  function combine(left, right) result(value)
    integer, intent(in) :: left, right
    integer :: value
    value = left + right
  end function combine

  subroutine announce(value)
    integer, intent(in) :: value
    print *, value
    return
  end subroutine announce

  recursive subroutine countdown(value)
    integer, intent(in) :: value
    if (value > 0) then
      print *, value
      call countdown(value - 1)
    end if
  end subroutine countdown
end program procedures
