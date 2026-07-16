program optional_writeback
  implicit none
  integer :: scalar = 40
  integer :: elements(2) = [10, 20]
  integer :: values(4) = [1, 2, 3, 4]
  integer :: output(2)
  integer :: target(3) = [5, 6, 7]
  integer :: matrix(2, 2) = reshape([1, 2, 3, 4], [2, 2])
  integer :: set_value
  call maybe_increment(scalar)
  call maybe_increment()
  call forward(scalar)
  call forward()
  call maybe_increment(elements(2))
  call maybe_scale(items=values(1:4:2))
  call maybe_scale()
  call maybe_fill(items=output)
  call maybe_fill(target(2:3))
  call maybe_fill()
  call maybe_set(value=set_value)
  call maybe_set()
  call maybe_corner(matrix)
  call maybe_corner()
  print *, scalar, elements(2), values(1), values(3), output(1), output(2), &
           target(1), target(2), target(3), set_value, matrix(2, 2)
contains
  subroutine maybe_increment(value)
    integer, intent(inout), optional :: value
    if (present(value)) then
      value = value + 2
    end if
  end subroutine maybe_increment

  subroutine forward(value)
    integer, intent(inout), optional :: value
    call maybe_increment(value)
  end subroutine forward

  subroutine maybe_scale(items)
    integer, intent(inout), optional :: items(:)
    integer :: index
    if (present(items)) then
      do index = 1, size(items)
        items(index) = items(index) * 2
      end do
    end if
  end subroutine maybe_scale

  subroutine maybe_fill(items)
    integer, intent(out), optional :: items(:)
    if (present(items)) then
      items(1) = 20
      items(2) = 22
    end if
  end subroutine maybe_fill

  subroutine maybe_set(value)
    integer, intent(out), optional :: value
    if (present(value)) then
      value = 42
    end if
  end subroutine maybe_set

  subroutine maybe_corner(items)
    integer, intent(inout), optional :: items(:, :)
    if (present(items)) then
      items(2, 2) = 42
    end if
  end subroutine maybe_corner
end program optional_writeback
