// This code is part of the project "Theoretically Efficient Parallel Graph
// Algorithms Can Be Fast and Scalable", presented at Symposium on Parallelism
// in Algorithms and Architectures, 2018.
// Copyright (c) 2018 Laxman Dhulipala, Guy Blelloch, and Julian Shun
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all  copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#pragma once

#include <queue>
#include <stack>
#include <vector>
#include "ClusteredGraph.h"
#include "gbbs/gbbs.h"

namespace gbbs {
namespace approx_average_linkage {



template <class Weights>
struct heap_entry {
  using W = typename Weights::weight_type;
  using key_t = uintE;  // our_id
  using val_t = std::pair<uintE, W>;      // (ngh_id, weight)
  using aug_t = W;      // aggregated weight
  static inline bool comp(key_t a, key_t b) { return a < b; }
  static aug_t get_empty() { return Weights::id(); }  // check
  static aug_t from_entry(key_t k, val_t v) { return v.second; }  // v
  static aug_t combine(aug_t a, aug_t b) { return Weights::augmented_combine(a, b); }  // used to select min/max edges based on similarity/dissimilarity clustering.
};

template <class Weights,
          // provides get_weight : () -> Weights::weight_type which is the
          // datatype that is stored for each edge incident to a _cluster_. This
          // could involve more than simply storing the underlying weight, or
          // could internally be a representation like gbbs::empty.
          template <class WW> class w_vertex,
          class IW>  // the weight type of the underlying graph
auto HAC(symmetric_graph<w_vertex, IW>& G, Weights& weights, double epsilon = 0.1) {
  using W = typename Weights::weight_type;  // potentially a more complex type than IW

  using pq_elt = std::tuple<uintE, uintE, W>;
  using edge = std::pair<uintE, W>;

  using clustered_graph = gbbs::approx_average_linkage::clustered_graph<Weights, IW, w_vertex>;
  // using cluster_id = clustering::cluster_id;

  size_t n = G.n;

  // This object stores a representation of both the original clusters and the
  // clusters formed throughout the clustering process.
  auto CG = clustered_graph(G, weights, epsilon);


  // PQ stores O(n) values --- one per cluster. The PQ values are (cluster,
  // cluster, weight) triples.
  auto pq_cmp = [](const pq_elt& l, const pq_elt& r) {
    return std::get<2>(l) < std::get<2>(r);
  };
  std::priority_queue<pq_elt, std::vector<pq_elt>, decltype(pq_cmp)> pq(pq_cmp);

  //Compute the min-weight edge incident to each original vertex
  parlay::sequence<edge> best_neighbors(
      n, std::make_pair(UINT_E_MAX, std::numeric_limits<W>::max()));
   parallel_for(0, n, [&](size_t i) {
     auto edge_option = CG.clusters[i].highest_priority_edge();
     if (edge_option.has_value()) {
       best_neighbors[i] = edge_option.value();
     }
  });

  std::cout << "Starting clustering" << std::endl;

  // Build initial heap elements (min-weight edge incident to each vertex).
  using heap_map = aug_map<heap_entry<Weights>>;
  using heap_ent = std::pair<uintE, std::pair<uintE, W>>;
  auto ngh_seq = parlay::delayed_seq<heap_ent>(n, [&] (size_t i) {
    return std::make_pair(i, best_neighbors[i]);
  });
  auto initial_heap_elts = parlay::filter(ngh_seq, [&] (auto e) {
    return e.second.first != UINT_E_MAX;
  });

  // Build the heap. All edges stored in the heap are min-edges. An entry for
  // key i in the heap means that the value stored is the min-weight edge
  // currently out of cluster i.
  //
  // The augmented value is computed using max/min, based on whether we are in
  // the similarity or dissimilarity setting.
  auto the_heap = heap_map(initial_heap_elts);
  std::cout << "heap size = " << the_heap.size() << std::endl;

  size_t unites = 0;

  // Cluster while the heap contains some active edge.
  while (the_heap.size() > 0) {
    W m = the_heap.aug_val();
    auto entry = the_heap.aug_eq(m);
    assert(entry.has_value());
    uintE u = (*entry).first;
    uintE v = (*entry).second.first;
    W wgh = (*entry).second.second;

    // std::cout << "Extracted (u,v) edge = " << u << ", " << v << std::endl;

    debug(assert(m == wgh););
    assert(CG.is_active(u));

    if (!CG.is_active(v)) {
      // Our (u's) highest-pri edge points to a cluster that has already been
      // merged.
      assert(CG.clusters[u].neighbor_size() > 0);
      auto min_edge = *(CG.clusters[u].highest_priority_edge());
      auto ent = std::make_pair(u, min_edge);
      assert(min_edge.first != v);
      assert(CG.is_active(min_edge.first));

      auto heap_save = std::move(the_heap);
      auto rem_u = heap_map::remove(std::move(heap_save), u);
      rem_u.insert(ent);
      the_heap = std::move(rem_u);

      // std::cout << "Inactive neighbor: " << v << std::endl;

      continue;
    }
    assert(CG.is_active(v));

    // Get the current best edge from u and v. If the best edge is not (u,v),
    // then update the heap and restart. The total work due to these steps can
    // also be bounded as O(m\logn) * W(heap-update) = O(m\log^2n).
    bool restart = false;
    {
      auto min_edge = *(CG.clusters[u].highest_priority_edge());
      auto ngh_u = min_edge.first;
      if (ngh_u != v && (min_edge.second > wgh)) {
        auto true_v_wgh = *CG.clusters[u].neighbors.find(v);
//        std::cout << "(u,v) = " << u <<"," << v << " but ngh_u = " << ngh_u << std::endl;
//        std::cout << "wgh = " << Weights::AsString(wgh) << " but ngh_u wgh = " << Weights::AsString(min_edge.second) << " cur weight to v =" << Weights::AsString(true_v_wgh.second) << " weight comparison = " << (min_edge.second > wgh) << std::endl;
        auto ent = std::make_pair(u, min_edge);
        the_heap.insert(ent);  // replace
        restart = true;

//        using neighbor_map = typename clustered_graph::neighbor_map;
//        auto entries_u = neighbor_map::entries(CG.clusters[u].neighbors);
//        for (size_t i=0; i<entries_u.size(); i++) {
//          std::cout << "ngh = " << entries_u[i].first << " " << entries_u[i].second.first << " wgh = " << Weights::AsString(entries_u[i].second.second) << std::endl;
//        }

        // exit(0);
      }
    }
    {
      auto min_edge = *(CG.clusters[v].highest_priority_edge());
      auto ngh_v = min_edge.first;
      if (ngh_v != u && (min_edge.second > wgh)) {
//        std::cout << "(u,v) = " << u <<"," << v << " but ngh_v = " << ngh_v << std::endl;
//        std::cout << "wgh = " << Weights::AsString(wgh) << " but ngh_v wgh = " << Weights::AsString(min_edge.second) << " weight comparison = " << (min_edge.second > wgh) << std::endl;
        auto ent = std::make_pair(v, min_edge);
        the_heap.insert(ent);  // replace

//        using neighbor_map = typename clustered_graph::neighbor_map;
//        auto entries_v= neighbor_map::entries(CG.clusters[v].neighbors);
//        for (size_t i=0; i<entries_v.size(); i++) {
//          std::cout << "ngh = " << entries_v[i].first << " " << entries_v[i].second.first << " wgh = " << Weights::AsString(entries_v[i].second.second) << std::endl;
//        }


        restart = true;
      }
    }
    if (restart) {
      std::cout << "Restarting!" << std::endl;
      continue;
    }


    unites++;
    uintE merged_id __attribute__((unused)) = CG.unite(u, v, wgh);
    debug(
    std::cout << "Min weight edge of weight = " << Weights::AsString(wgh) << " between " << u << " " << v << std::endl;
    std::cout << "Done unite. Merged into " << merged_id << std::endl;);

    auto heap_save = std::move(the_heap);
    auto rem_u = heap_map::remove(std::move(heap_save), u);
    assert(rem_u.contains(v));
    auto rem_v = heap_map::remove(std::move(rem_u), v);

    assert(CG.is_active(merged_id));
    auto min_edge_opt = CG.clusters[merged_id].highest_priority_edge();

    if (min_edge_opt.has_value()) {
      auto min_edge = *min_edge_opt;
      auto ent = std::make_pair(merged_id, min_edge);
      rem_v.insert(ent);
    }
    the_heap = std::move(rem_v);
  }

  std::cout << "Performed " << unites << " many merges. " << std::endl;

  return CG.get_dendrogram();
}

}  // namespace approx_average_linkage
}  // namespace gbbs

