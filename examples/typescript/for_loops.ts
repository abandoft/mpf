let total: number = 0;

for (let index: number = 0; index < 10; index++) {
  if (index === 2) {
    continue;
  }
  if (index === 6) {
    break;
  }
  total = total + index;
}

console.log(total);
