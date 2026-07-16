program argument_association
  implicit none
  integer :: reordered, absent, supplied, forwarded_absent, forwarded_value, skipped_middle
  reordered = combine(right=2, left=40)
  absent = with_default(40)
  supplied = with_default(increment=2, required=40)
  forwarded_absent = forward(40)
  forwarded_value = forward(40, 2)
  skipped_middle = choose(factor=3, base=10)
  print *, reordered, absent, supplied, forwarded_absent, forwarded_value, skipped_middle
contains
  integer function combine(left, right) result(value)
    integer, intent(in) :: left, right
    value = left + right
  end function combine

  integer function with_default(required, increment) result(value)
    integer, intent(in) :: required
    integer, intent(in), optional :: increment
    if (present(increment)) then
      value = required + increment
    else
      value = required + 1
    end if
  end function with_default

  integer function forward(required, increment) result(value)
    integer, intent(in) :: required
    integer, intent(in), optional :: increment
    value = with_default(required, increment)
  end function forward

  integer function choose(base, increment, factor) result(value)
    integer, intent(in) :: base
    integer, intent(in), optional :: increment, factor
    value = base
    if (present(increment)) then
      value = value + increment
    end if
    if (present(factor)) then
      value = value * factor
    end if
  end function choose
end program argument_association
