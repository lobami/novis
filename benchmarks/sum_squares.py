# Python benchmark: sum of squares of the first N integers.
# Mirrors benchmarks/sum_squares.novis exactly.
import sys

n = 1_000_000
xs = [i + 1 for i in range(n)]
print(sum(xs))
