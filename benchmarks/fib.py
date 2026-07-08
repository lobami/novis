# Python benchmark: fibonacci
# Recursive, naive (no memoization) — identical to benchmarks/fib.novis
import sys

def fib(n: int) -> int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

result = fib(30)
print(result)
