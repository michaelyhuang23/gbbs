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

#include <stack>
#include <unordered_set>
#include <vector>

#include "gbbs/gbbs.h"
#include "gbbs/dynamic_graph_io.h"

namespace gbbs {

struct Level {
  std::unordered_set<uintE> set;
  std::vector<uintE> vector;
  static constexpr int high_boundary = 1000;
  static constexpr int low_boundary = 500;
  bool use_vector;

  Level() : use_vector(true) {}

  void set_to_vector() {
    for (const auto& v : set) {
      vector.push_back(v);
    }
    set.clear();
    use_vector = true;
  }

  void vector_to_set() {
    for (const auto& v : vector) {
      set.insert(v);
    }
    vector.clear();
    use_vector = false;
  }

  void erase(uintE v) {
    if (use_vector) vector.erase(std::find(vector.begin(), vector.end(), v));
    else {
      set.erase(set.find(v));
      if (set.size() < low_boundary) set_to_vector();
    }
  }

  template <class Iterator>
  void erase(Iterator it) {
    if (use_vector) vector.erase(it);
    else {
      set.erase(it);
      if (set.size() < low_boundary) set_to_vector();
    }
  }

  void insert(uintE v) {
    if (use_vector) {
      vector.push_back(v);
      if (vector.size() > high_boundary) vector_to_set();
    } else set.insert(v);
  }

  template <class F>
  void iterate(F f) {
    if (use_vector) {
      for (const auto& v : vector) {
        f(v);
      }
    } else {
      for (const auto& v : set) {
        f(v);
      }
    }
  }

  template <class F>
  void special_iterate(F f) {
    if (use_vector) {
      auto end = set.end();
      for (auto it = vector.begin(); it != vector.end();) {
        f(it, end);
      }
    } else {
      auto end = vector.end();
      for (auto it = set.begin(); it != set.end();) {
        f(end, it);
      }
    }
  }

  size_t size() const {
    if (use_vector) return vector.size();
    return set.size();
  }
};

struct LDS {
  size_t n;  // number of vertices
  static constexpr double delta = 9.0;
  static constexpr double upper_constant = (2 + static_cast<double>(3) / delta);
  static constexpr double epsilon = 3.0;
  static constexpr double one_plus_eps = 1 + epsilon;

  inline static double group_degree(size_t group) {
    return pow(one_plus_eps, group);
  }

  size_t total_work;

  //using level = std::unordered_set<uintE>;
  using down_neighbors = std::vector<Level>;
  using up_neighbors = Level;
  using edge_type = std::pair<uintE, uintE>;

  struct LDSVertex {
    uintE level;  // level of this vertex
    down_neighbors
        down;         // neighbors in levels < level, bucketed by their level.
    up_neighbors up;  // neighbors

    LDSVertex() : level(0) {}

    void insert_neighbor(uintE v, uintE l_v) {
      if (l_v < level) {
        assert(down.size() > l_v);
        down[l_v].insert(v);
      } else {
        up.insert(v);
      }
    }

    void remove_neighbor(uintE v, uintE l_v) {
      if (l_v < level) {
        down[l_v].erase(v);
      } else {
        up.erase(v);
      }
    }


    inline bool upper_invariant(const size_t levels_per_group) const {
      uintE group = level / levels_per_group;
      return up.size() <=
             static_cast<size_t>(upper_constant * group_degree(group));
    }

    inline bool lower_invariant(const size_t levels_per_group) const {
      if (level == 0) return true;
      uintE lower_group = (level - 1) / levels_per_group;
      auto up_size = up.size();
      auto prev_level_size = down[level - 1].size();
      return (up_size + prev_level_size) >=
             static_cast<size_t>(
                 group_degree(lower_group));  // needs a floor or ceil?
    }
  };

  // number of inner-levels per group,  O(\log n) many.
  size_t levels_per_group;
  parlay::sequence<LDSVertex> L;
  std::stack<uintE> Dirty;

  LDS(size_t n) : n(n) {
    levels_per_group = ceil(log(n) / log(one_plus_eps));
    // levels_per_group = parlay::log2_up(n);
    L = parlay::sequence<LDSVertex>(n);
  }

  uintE get_level(uintE ngh) { return L[ngh].level; }

  // Moving u from level to level - 1.
  template <class Levels>
  void level_decrease(uintE u, Levels& L) {
    total_work++;
    uintE level = L[u].level;
    auto& up = L[u].up;
    assert(level > 0);
    auto& prev_level = L[u].down[level - 1];

    prev_level.iterate([&](const uintE& ngh) {
    //for (const auto& ngh : prev_level) {
      up.insert(ngh);
    });
    L[u].down.pop_back();  // delete the last level in u's structure.

    up.iterate([&](const uintE& ngh) {
    //for (const auto& ngh : up) {
      if (get_level(ngh) == level) {
        L[ngh].up.erase(u);
        L[ngh].down[level - 1].insert(u);

      } else if (get_level(ngh) > level) {
        L[ngh].down[level].erase(u);
        L[ngh].down[level - 1].insert(u);

        if (get_level(ngh) == level + 1) {
          Dirty.push(ngh);
        }
      } else {
        // u is still "up" for this ngh, no need to update.
        assert(get_level(ngh) == (level - 1));
      }
    });
    L[u].level--;  // decrease level
  }

  // Moving u from level to level + 1.
  template <class Levels>
  void level_increase(uintE u, Levels& L) {
    total_work++;
    uintE level = L[u].level;
    std::vector<uintE> same_level;
    auto& up = L[u].up;

    up.special_iterate([&](std::vector<uintE>::iterator& vec_it, std::unordered_set<uintE>::iterator& set_it) {
    //for (auto it = up.begin(); it != up.end();) {
      bool use_vec = (vec_it != up.vector.end());
      uintE ngh = use_vec ? *vec_it : *set_it; //*it;
      if (L[ngh].level == level) {
        same_level.emplace_back(ngh);
        //it = up.erase(it);
        if (use_vec) vec_it = up.vector.erase(vec_it);
        else set_it = up.set.erase(set_it);
        // u is still "up" for this ngh, no need to update.
      } else {
        //it++;
        if (use_vec) vec_it++;
        else set_it++;
        // Must update ngh's accounting of u.
        if (L[ngh].level > level + 1) {
          L[ngh].down[level].erase(u);
          L[ngh].down[level + 1].insert(u);
        } else {
          assert(L[ngh].level == level + 1);
          L[ngh].down[level].erase(u);
          L[ngh].up.insert(u);

          Dirty.push(ngh);
        }
      }
    });
    // We've now split L[u].up into stuff in the same level (before the
    // update) and stuff in levels >= level + 1. Insert same_level elms
    // into down.
    auto& down = L[u].down;
    down.emplace_back(Level());//std::unordered_set<uintE>());
    assert(down.size() == level + 1);  // [0, level)
    for (const auto& ngh : same_level) {
      down[level].insert(ngh);
    }
    L[u].level++;  // Increase level.
  }

  void fixup() {
    while (!Dirty.empty()) {
      uintE u = Dirty.top();
      Dirty.pop();
      if (!L[u].upper_invariant(levels_per_group)) {
        // Move u to level i+1.
        level_increase(u, L);
        Dirty.push(u);  // u might need to move up more levels.
        // std::cout << "(move up) pushing u = " << u << std::endl;
      } else if (!L[u].lower_invariant(levels_per_group)) {
        level_decrease(u, L);
        Dirty.push(u);  // u might need to move down more levels.
        // std::cout << "(move down) pushing u = " << u << std::endl;
      }
    }
  }

  /*bool edge_exists(edge_type e) {
    auto[u, v] = e;
    auto l_u = L[u].level;
    auto l_v = L[v].level;
    if (l_u < l_v) {  // look in up(u)
      return (L[u].up.find(v) != L[u].up.end());
    } else {  // look in up(v)
      return (L[v].up.find(u) != L[v].up.end());
    }
  }*/

  bool insert_edge(edge_type e) {
    //if (edge_exists(e)) return false;
    auto[u, v] = e;
    auto l_u = L[u].level;
    auto l_v = L[v].level;
    L[u].insert_neighbor(v, l_v);
    L[v].insert_neighbor(u, l_u);

    Dirty.push(u);
    Dirty.push(v);
    fixup();
    return true;
  }

  bool delete_edge(edge_type e) {
    //if (!edge_exists(e)) return false;
    auto[u, v] = e;
    auto l_u = L[u].level;
    auto l_v = L[v].level;
    L[u].remove_neighbor(v, l_v);
    L[v].remove_neighbor(u, l_u);

    Dirty.push(u);
    Dirty.push(v);
    fixup();
    return true;
  }

  void check_invariants() {
    bool invs_ok = true;
    for (size_t i = 0; i < n; i++) {
      bool upper_ok = L[i].upper_invariant(levels_per_group);
      bool lower_ok = L[i].lower_invariant(levels_per_group);
      assert(upper_ok);
      assert(lower_ok);
      invs_ok &= upper_ok;
      invs_ok &= lower_ok;
    }
    std::cout << "invs ok is: " << invs_ok << std::endl;
  }

  uintE max_coreness() {
    auto levels = parlay::delayed_seq<uintE>(n, [&] (size_t i) { return L[i].level; });
    uintE max_level = pbbslib::reduce_max(levels);
    uintE max_group = group_for_level(max_level);
    return group_degree(max_group);
  }

  inline uintE group_for_level(uintE level) const {
    return level / levels_per_group;
  }
};

template <class Graph>
inline void RunLDS(Graph& G, LDS& layers) {
  using W = typename Graph::weight_type;
  size_t n = G.n;
  for (size_t i = 0; i < n; i++) {
    auto map_f = [&](const uintE& u, const uintE& v, const W& wgh) {
      if (u < v) {
        layers.insert_edge({u, v});
      }
    };
    G.get_vertex(i).out_neighbors().map(map_f, /* parallel = */ false);
  }

  std::cout << "Finished all insertions!" << std::endl;
  layers.check_invariants();
  std::cout << "Coreness estimate = " << layers.max_coreness() << std::endl;

  for (size_t i = 0; i < n; i++) {
    auto map_f = [&](const uintE& u, const uintE& v, const W& wgh) {
      if (u < v) {
        layers.delete_edge({u, v});
      }
    };
    G.get_vertex(i).out_neighbors().map(map_f, /* parallel = */ false);
  }

  std::cout << "Finished all deletions!" << std::endl;
  layers.check_invariants();

  std::cout << "Coreness estimate = " << layers.max_coreness() << std::endl;

  size_t sum_lev = 0;
  for (size_t i = 0; i < G.n; i++) {
    sum_lev += layers.L[i].level;
  }
  std::cout << "sum_lev = " << sum_lev << std::endl;

  std::cout << "Total level increases and decreases: " << layers.total_work
            << std::endl;
}

template <class W>
inline void RunLDS(BatchDynamicEdges<W>& batch_edge_list, LDS& layers) {
  for (std::size_t i = 0; i < batch_edge_list.edges.size(); i++) {
    auto batch = batch_edge_list.edges[i];
    parallel_for(0, batch.size(), [&](std::size_t j) {
      if (batch[j].insert) layers.insert_edge({batch[j].from, batch[j].to});
      else layers.delete_edge({batch[j].from, batch[j].to});
    });
    layers.check_invariants();
    std::cout << "Coreness estimate = " << layers.max_coreness() << std::endl;
  }
}

template <class Graph, class W>
inline void RunLDS(Graph& G, BatchDynamicEdges<W> batch_edge_list) {
  uintE max_vertex = std::max(uintE{G.n}, batch_edge_list.max_vertex);
  auto layers = LDS(max_vertex);
  if (G.n > 0) RunLDS(G, layers);
  if (batch_edge_list.max_vertex > 0) RunLDS(batch_edge_list, layers);
}

}  // namespace gbbs