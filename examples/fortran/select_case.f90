program select_case_demo
  implicit none
  integer :: result
  character(len=2) :: grade = 'b '
  logical :: enabled = .true.

  select case (choose(4))
  case (:0)
    result = 1
  case (1, 3:5, 9:)
    result = 40
  case default
    result = 2
  end select

  select case (grade)
  case ('a')
    result = result + 1
  case ('b':'c')
    result = result + 2
  case default
    result = 0
  endselect

  select case (enabled)
  case (.true.)
    select case (result)
    case (42)
      result = result + 0
    case default
      result = 0
    end select
  case default
    result = 0
  end select

  print *, result
contains
  integer function choose(input) result(value)
    integer, intent(in) :: input
    print *, 7
    value = input
  end function choose
end program select_case_demo
