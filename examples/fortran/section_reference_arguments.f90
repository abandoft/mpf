program section_reference
  implicit none
  integer :: values(5) = [1, 2, 3, 4, 5]
  integer :: matrix(2, 4) = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 4])
  integer :: answer
  call bump(values(2))
  call place(values(4))
  call scale(values(1:5:2))
  answer = adjust(values(3:5))
  call replace(matrix(:, 2))
  call replace_block(matrix(:, 3:4))
  print *, values(2), values(1), values(3), values(4), values(5), answer, &
           matrix(1, 2), matrix(2, 2), matrix(1, 3), matrix(2, 3), matrix(1, 4), matrix(2, 4)
contains
  subroutine bump(value)
    integer, intent(inout) :: value
    value = value + 40
  end subroutine bump

  subroutine place(value)
    integer, intent(out) :: value
    value = 24
  end subroutine place

  subroutine scale(items)
    integer, intent(inout) :: items(:)
    integer :: index
    do index = 1, size(items)
      items(index) = items(index) + 10
    end do
  end subroutine scale

  integer function adjust(items) result(total)
    integer, intent(inout) :: items(:)
    items(1) = items(1) + 1
    total = sum(items)
  end function adjust

  subroutine replace(items)
    integer, intent(out) :: items(:)
    items(1) = 20
    items(2) = 22
  end subroutine replace

  subroutine replace_block(items)
    integer, intent(out) :: items(:, :)
    items(1, 1) = 30
    items(2, 1) = 32
    items(1, 2) = 40
    items(2, 2) = 42
  end subroutine replace_block
end program section_reference
