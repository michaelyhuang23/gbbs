---
id: connectivity
title: Connectivity
---


## Problem Specification
#### Input
$G=(V, E)$, an undirected graph on $n$ vertices. The input graph can
either be weighted or unweighted.

#### Output
$C$, a [mapping](/docs/benchmarks/definitions) where $C[v]$ is a unique id
between $[0, n)$ representing the component of $v$ s.t. $C[u] = C[v]$
if and only if $u$ and $v$ are in the same connected component in $G$.


## Algorithm Implementations
We provide multiple implementations of connectivity in GBBS. The
primary implementation is based on the low-diameter decomposition
based algorithm from [Shun et
al.](https://dl.acm.org/doi/10.1145/2612669.2612692).

The code for the primary implementation is available
[here](https://github.com/ldhulipala/gbbs/tree/master/benchmarks/Connectivity/WorkEfficientSDB).


## Cost Bounds

The algorithm runs in $O(n + m)$ expected work and $O(\log^{3} n)$
depth w.h.p., and the proof can be found in the Shun et al. paper.


## Compiling and Running

The benchmark can be compiled by running:
```
bazel build -c opt //benchmarks/Connectivity/WorkEfficientSDB14:BFS
```

It can then be run on a test input graph in the *uncompressed format* as follows:
```
numactl -i all ./bazel-bin/benchmarks/Connectivity/WorkEfficientSDB14/Connectivity_main -s -m -src 1 inputs/rMatGraph_J_5_100
```

It can then be run on a test input graph in the *compressed format* as follows:
```
numactl -i all ./bazel-bin/benchmarks/Connectivity/WorkEfficientSDB14/Connectivity_main -s -c -m -src 1 inputs/rMatGraph_J_5_100.bytepda
```