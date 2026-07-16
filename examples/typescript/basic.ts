export function accumulate(limit: number, step: number = 1): number {
  let total: number = 0;
  let cursor: number = 0;
  while (cursor < limit) {
    total = total + step;
    cursor = cursor + 1;
  }
  if (total === 42) {
    total = total + 0;
  } else {
    total = 0;
  }
  return total;
}

const answer: number = accumulate(42);
console.log(answer);
