let value: number = 1;

if (true) {
  let value: string = "inner";
  console.log(value);
}

if (true) {
  value = 42;
}

console.log(value);
