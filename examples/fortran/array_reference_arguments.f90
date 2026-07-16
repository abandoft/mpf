program array_reference
  implicit none
  integer :: values(3) = [1, 2, 3]
  integer :: output(2)
  integer :: matrix(2, 2) = reshape([1, 2, 3, 4], [2, 2])
  call scale(values)
  call fill(output)
  call set_corner(matrix)
  print *, values(1), values(2), values(3), output(1), output(2), matrix(2, 2)
contains
  subroutine scale(items)
    integer, intent(inout) :: items(:)
    integer :: index
    do index = 1, size(items)
      items(index) = items(index) * 2
    end do
  end subroutine scale

  subroutine fill(items)
    integer, intent(out) :: items(:)
    items(1) = 20
    items(2) = 22
  end subroutine fill

  subroutine set_corner(items)
    integer, intent(inout) :: items(:, :)
    items(2, 2) = 42
  end subroutine set_corner
end program array_reference
