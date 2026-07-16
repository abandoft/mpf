program reference_arguments
  implicit none
  integer :: value = 40, produced, inferred, adjusted = 41
  integer :: written, answer

  call outer_increment(value)
  call produce(produced)
  call infer_output(inferred)
  call infer_inout(adjusted)
  answer = compute(40, written)
  print *, value, produced, inferred, adjusted, answer, written
contains
  subroutine outer_increment(argument)
    integer, intent(inout) :: argument
    call increment(argument)
  end subroutine outer_increment

  subroutine increment(argument)
    integer, intent(inout) :: argument
    argument = argument + 2
  end subroutine increment

  subroutine produce(argument)
    integer, intent(out) :: argument
    argument = 42
  end subroutine produce

  subroutine infer_output(argument)
    integer :: argument
    argument = 42
  end subroutine infer_output

  subroutine infer_inout(argument)
    integer :: argument
    argument = argument + 1
  end subroutine infer_inout

  integer function compute(input, output) result(result_value)
    integer, intent(in) :: input
    integer, intent(out) :: output
    output = input + 1
    result_value = input + 2
  end function compute
end program reference_arguments
