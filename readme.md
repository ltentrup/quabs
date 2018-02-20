# QuAbS

QuAbS is a circuit-based QBF solver.

# Build

```bash
git clone https://github.com/ltentrup/quabs.git
cd quabs
git submodule init
git submodule update
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

# Run

```
./quabs ../test/unittests/test_sat.qcir
```
should return `r SAT`

# QCIR <-> QAIGER conversion

We provide a conversion from the quantified circuit format [QCIR](http://qbf.satisfiability.org/gallery/qcir-gallery14.pdf) to the quantified AIGER format [QAIGER](https://github.com/ltentrup/QAIGER).

```
./qcir2qaiger ../test/unittests/test_sat.qcir
```

should output

```
aag 8 2 0 1 6
2
4
16
6 4 3
8 6 1
10 5 2
12 10 1
14 13 9
16 14 1
i0 1 1
i1 2 2
c
Transformed to QAIGER file format (https://github.com/ltentrup/QAIGER)
using qcir2qaiger (https://github.com/ltentrup/quabs).
```