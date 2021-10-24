#!/bin/sh

check() {
  ./tmp
  if [ $? -eq 0 ]; then
    echo "test $1 passed."
    rm tmp
  else
    echo "test $1 failed."
    rm tmp
    exit 1
  fi
}

gcc -std=c11 -static -c -o hashmap.o hashmap_gcc.c -I ../src
gcc -static -g -o tmp ../src/util/hashmap.o hashmap.o
rm hashmap.o
check "hashmap.c"

assert() {
  expected="$1"
  input="$2"

  echo "$input" > tmp.c
  ../jcc tmp.c tmp.s
  gcc -static -o tmp tmp.s
  ./tmp
  actual="$?"

  rm tmp tmp.s tmp.c
  if [ "$actual" = "$expected" ]; then
    echo -e "\e[32m[ OK ]\e[m $input => $actual"
  else
    echo -e "\e[31m[ NG ]\e[m $input => $expected expected, but got $actual"
    exit 1
  fi
}

assert 3  "int main() { 1+2; }" 
assert 1  "int main() { 2-1; }" 
assert 8  "int main() { 2*4; }" 
assert 3  "int main() { 6/2; }" 
assert 10 "int main() { 2*4+2; }"
assert 1  "int main() { 5%2; }"
assert 49 "int main() { (2+5)*7; }"
assert 1  "int main() { (2+5)%(3-1); }"
assert 6  "int main() { 3<<1; }"
assert 32 "int main() { 8<<2; }"
assert 1  "int main() { 3>>1; }"
assert 4  "int main() { 8>>1; }"

assert $((1 > 0))  "int main() { 1 > 0; }"
assert $((1 < 0))  "int main() { 1 < 0; }"
assert $((0 > 1))  "int main() { 0 > 1; }"
assert $((0 < 1))  "int main() { 0 < 1; }"
assert $((1 <= 1)) "int main() { 1 <= 1; }"
assert $((1 <= 0)) "int main() { 1 <= 0; }"
assert $((0 <= 1)) "int main() { 0 <= 1; }"
assert $((1 >= 1)) "int main() { 1 >= 1; }"
assert $((1 >= 0)) "int main() { 1 >= 0; }"
assert $((0 >= 1)) "int main() { 0 >= 1; }"
assert $((0 == 0)) "int main() { 0 == 0; }"
assert $((0 == 1)) "int main() { 0 == 1; }"
assert $((0 != 0)) "int main() { 0 != 0; }"
assert $((0 != 1)) "int main() { 0 != 1; }"

assert $((42 & 32)) "int main() { 42 & 32; }"
assert $((13 & 2))  "int main() { 13 & 2; }"
assert $((24 & 42)) "int main() { 24 & 42; }"

assert $((42 ^ 32)) "int main() { 42 ^ 32; }"
assert $((13 ^ 2))  "int main() { 13 ^ 2; }"
assert $((24 ^ 42)) "int main() { 24 ^ 42; }"

assert $((42 | 32)) "int main() { 42 | 32; }"
assert $((13 | 2))  "int main() { 13 | 2; }"
assert $((24 | 42)) "int main() { 24 | 42; }"

assert $((0 && 0)) "int main() { 0 && 0; }"
assert $((0 && 1)) "int main() { 0 && 1; }"
assert $((1 && 1)) "int main() { 1 && 1; }"

assert $((0 || 0)) "int main() { 0 || 0; }"
assert $((0 || 1)) "int main() { 0 || 1; }"
assert $((1 || 1)) "int main() { 1 || 1; }"

assert 2 "int main() { 0 ? 1 : 2; }"
assert 3 "int main() { 0 || 1 ? 3 : 1; }"
assert 1 "int main() { 0 && 1 ? 3 : 1; }"
assert 5 "int main() { 1 ? 1 ? 5 : 3 : 2; }"
assert 2 "int main() { 0 ? 3 : 0 ? 6 : 2; }"

assert 5 "int main() { 1 + 2; 3 + 2; }"
assert 5 "int main() { int a; 3 + 2; }"
assert 5 "int main() { int a; int b; 3 + 2; }"
