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

#include "gbbs/gbbs.h"
#include "gbbs/julienne.h"
#include "benchmarks/KCore/JulienneDBS17/KCore.h"
#include <fstream>

namespace gbbs
{
	// bipartition gives the last vertex id in first partition
	// size_t bipartition = P.getOptionLongValue("-bi", 2);

	template <class Graph>
	inline void BiCore(Graph &G, size_t num_buckets = 16, size_t bipartition = 2, size_t peel_core_alpha=0, size_t peel_core_beta=0)
	{
		std::cout << "begin" << std::endl;

		const size_t n = G.n;
		const size_t n_b = n - bipartition - 1;
		const size_t n_a = bipartition + 1;

		// AlphaMax[v][B]
		auto AlphaMax = sequence<sequence<size_t>>(n_b, [&G, &n_a](size_t i){ return sequence<size_t>(1+G.get_vertex(i+n_a).out_degree(),[](size_t i){return 0;}); });
		// BetaMax[u][A]
		auto BetaMax = sequence<sequence<size_t>>(n_a, [&G](size_t i){ return sequence<size_t>(1+G.get_vertex(i).out_degree(),[](size_t i){return 0;}); });

		if(peel_core_alpha!=0){
			PeelFixA(G, BetaMax, AlphaMax, peel_core_alpha, bipartition, num_buckets);
			std::cout << "complete PeelFixA" << std::endl;
			return;
		}else if(peel_core_beta!=0){
			PeelFixB(G, BetaMax, AlphaMax, peel_core_beta, bipartition, num_buckets);
			std::cout << "complete PeelFixB" << std::endl;
			return;
		}
		auto ret = KCore(G, num_buckets);
		const uintE delta = static_cast<size_t>(pbbslib::reduce_max(ret));

		auto msgA = pbbslib::new_array_no_init<std::tuple<size_t,size_t,float_t>>(delta+1);
		auto msgB = pbbslib::new_array_no_init<std::tuple<size_t,size_t,float_t>>(delta+1);

		// for(size_t core = 1; core<=delta; core++){
		// 	timer t_in; t_in.start();
		// 	auto retA = PeelFixA(G, BetaMax, AlphaMax, core, bipartition, num_buckets);
		// 	msgA[core]=std::make_tuple(std::get<0>(retA),std::get<1>(retA),t_in.stop());
		// }

		// for(size_t core = 1; core<=delta; core++){
		// 	timer t_in; t_in.start();
	 	// 	auto retB = PeelFixB(G, BetaMax, AlphaMax, core, bipartition, num_buckets);
	 	// 	msgB[core]=std::make_tuple(std::get<0>(retB),std::get<1>(retB),t_in.stop());
		// }
		auto PeelFixAllA = [&](){
			par_for(1,delta+1,[&](size_t core){
				timer t_in; t_in.start();
				auto retA = PeelFixA(G, BetaMax, AlphaMax, core, bipartition, num_buckets);
				msgA[core]=std::make_tuple(std::get<0>(retA),std::get<1>(retA),t_in.stop());
			});
		};

		auto PeelFixAllB = [&](){
			par_for(1,delta+1,[&](size_t core){
				timer t_in; t_in.start();
				auto retB = PeelFixB(G, BetaMax, AlphaMax, core, bipartition, num_buckets);
				msgB[core]=std::make_tuple(std::get<0>(retB),std::get<1>(retB),t_in.stop());
			});
		};

		par_do(PeelFixAllA,PeelFixAllB);

		debug(for(size_t core=1; core<=delta; ++core) std::cout<<"coreA "<<core<<" "<<std::get<0>(msgA[core])<<" "<<std::get<1>(msgA[core])<<" "<<std::get<2>(msgA[core])<<'\n');
		debug(for(size_t core=1; core<=delta; ++core) std::cout<<"coreB "<<core<<" "<<std::get<0>(msgB[core])<<" "<<std::get<1>(msgB[core])<<" "<<std::get<2>(msgB[core])<<'\n');
	}

	template <class Graph>
	inline std::pair<size_t,size_t> PeelFixA(Graph &G, sequence<sequence<size_t>> &BetaMax, 
	sequence<sequence<size_t>> &AlphaMax, size_t alpha,
	 size_t bipartition = 2, size_t num_buckets=16)
	{
		timer bt,ft,pt;
		pt.start();

		const size_t n = G.n;
		const size_t n_b = n - bipartition - 1;
		const size_t n_a = bipartition + 1;

		size_t finished = 0, rho_alpha = 0, max_beta = 0;
		// [0, bipartition] interval for U
		// [bipartition+1, n-1]  interval V
		auto em = hist_table<uintE, uintE>(std::make_tuple(UINT_E_MAX, 0), (size_t)G.m / 50);

		auto D =
			sequence<uintE>(n, [&](size_t i) {
				return G.get_vertex(i).out_degree();
			});

		auto mask = sequence<std::tuple<bool, uintE>>(n_a, [&](size_t i) {
			return std::make_tuple<bool, uintE>((G.get_vertex(i).out_degree() < alpha)&(i<n_a), 0);
		});

		auto uDel = vertexSubsetData<uintE>(n_a, std::move(mask));

		auto cond_fu = [&D, &alpha](const uintE &u) { return D[u] >= alpha; };
		auto cond_fv = [&D, &max_beta](const uintE &v) { return D[v] > max_beta; };
		// instead of tracking whether a vertex is peeled or not using a boolean arr, we can just see whether its degree is above or below the cutoff

		auto clearZeroV = [&](const std::tuple<uintE, uintE> &p)
			-> const std::optional<std::tuple<uintE, uintE>> {
			uintE v = std::get<0>(p), edgesRemoved = std::get<1>(p);
			uintE new_deg = D[v] - edgesRemoved;
			D[v] = new_deg;
			if (new_deg == 0)
				return wrap(v, 0);
			return std::nullopt;
		};

		auto clearU = [&](const std::tuple<uintE, uintE> &p)
			-> const std::optional<std::tuple<uintE, uintE>> {
			uintE u = std::get<0>(p), edgesRemoved = std::get<1>(p);
			uintE new_deg = D[u] - edgesRemoved;
			D[u] = new_deg;
			if (new_deg < alpha)
			{
				if(max_beta>0)
					pbbslib::write_max(&BetaMax[u][alpha],max_beta);
				return wrap(u, 0);
			}
			return std::nullopt;
		};

		// peels all vertices in U which are < alpha, and repeatedly peels vertices in V which has deg == 0
		while (!uDel.isEmpty())
		{
			vertexSubsetData<uintE> vDel = nghCount(G, uDel, cond_fv, clearZeroV, em, no_dense);
			uDel = nghCount(G, vDel, cond_fu, clearU, em, no_dense);
		}

		size_t vCount = 0;

		auto vD =
			sequence<uintE>(n, [&](size_t i) {
				if (i <= bipartition || D[i] == 0)
					return std::numeric_limits<uintE>::max();
				return D[i];
			});

		auto bbuckets = make_vertex_buckets(n,vD,increasing,num_buckets);
		// make num_buckets open buckets such that each vertex i is in D[i] bucket
		// note this i value is not real i value; realI = i+bipartition+1 or i+n_a

		vCount = pbbslib::reduce_add(sequence<uintE>(n_b, [&](size_t i) {return D[i+n_a]>0;}));

		auto getVBuckets = [&](const std::tuple<uintE, uintE> &p)
			-> const std::optional<std::tuple<uintE, uintE>> {
			uintE v = std::get<0>(p), edgesRemoved = std::get<1>(p);
			uintE deg = D[v];
			uintE new_deg = std::max(deg - edgesRemoved, static_cast<uintE>(max_beta));
			D[v] = new_deg;
			return wrap(v, bbuckets.get_bucket(new_deg));
		};
		pt.stop();

		while (finished != vCount)
		{
			bt.start();
			auto vbkt = bbuckets.next_bucket();
			bt.stop();
			max_beta = std::max(max_beta, vbkt.id);
			if (vbkt.id == 0)
				continue;
			auto activeV = vertexSubset(n, std::move(vbkt.identifiers)); // container of vertices
			finished += activeV.size();

			par_for(0, activeV.size(), [&](size_t i) {
				size_t index = activeV.vtx(i)-n_a;
				par_for(1, max_beta, [&](size_t j) {
					pbbslib::write_max(&AlphaMax[index][j],alpha);
				});
			});
			ft.start();
			vertexSubsetData deleteU = nghCount(G, activeV, cond_fu, clearU, em, no_dense);
			// "deleteU" is a wrapper storing a sequence id of deleted vertices in U

			vertexSubsetData movedV = nghCount(G, deleteU, cond_fv, getVBuckets, em, no_dense);
			// "movedV" is a wrapper storing a sequence of tuples like (id, newBucket)
			ft.stop();
			bt.start();
			bbuckets.update_buckets(movedV);
			bt.stop();
			rho_alpha++;
		}
		bbuckets.del();
		em.del();
		return std::pair<size_t,size_t>(rho_alpha,max_beta);
	}

	template <class Graph>
	inline std::pair<size_t,size_t> PeelFixB(Graph &G, sequence<sequence<size_t>> &BetaMax, 
	sequence<sequence<size_t>> &AlphaMax, size_t beta,
	size_t bipartition = 2, size_t num_buckets=16)
	{
		timer bt,ft,pt;
		pt.start();
		const size_t n = G.n;
		const size_t n_b = n - bipartition - 1;
		const size_t n_a = bipartition + 1;

		size_t finished = 0, rho_beta = 0, max_alpha = 0;

		auto em = hist_table<uintE, uintE>(std::make_tuple(UINT_E_MAX, 0), (size_t)G.m / 50);

		auto D =
			sequence<uintE>(n, [&](size_t i) {
				return G.get_vertex(i).out_degree();
			});

		auto mask = sequence<std::tuple<bool, uintE>>(n, [&](size_t i) {
			if (i < n_a) return std::make_tuple<bool, uintE>(false, 0);
			return std::make_tuple<bool, uintE>(G.get_vertex(i).out_degree() < beta, 0);
		});
		auto vDel = vertexSubsetData<uintE>(n, std::move(mask));

		auto cond_fv = [&D, &beta](const uintE &v) { return D[v] >= beta; };
		auto cond_fu = [&D, &max_alpha](const uintE &u) { return D[u] > max_alpha; };

		// if the U list is empty
		auto clearZeroU = [&](const std::tuple<uintE, uintE> &p)
			-> const std::optional<std::tuple<uintE, uintE>> {
			uintE u = std::get<0>(p), edgesRemoved = std::get<1>(p);
			uintE new_deg = D[u] - edgesRemoved;
			D[u] = new_deg;
			if (new_deg == 0)
				return wrap(u, 0);
			return std::nullopt;
		};

		auto clearV = [&](const std::tuple<uintE, uintE> &p)
			-> const std::optional<std::tuple<uintE, uintE>> {
			uintE v = std::get<0>(p), edgesRemoved = std::get<1>(p);
			uintE new_deg = D[v] - edgesRemoved;
			D[v] = new_deg;
			if (new_deg < beta)
			{
				if(max_alpha>0)
					pbbslib::write_max(&AlphaMax[v-n_a][beta],max_alpha);
				return wrap(v, 0);
			}
			return std::nullopt;
		};

		// nghCount counts the # of neighbors
		while (!vDel.isEmpty())
		{
			vertexSubsetData<uintE> uDel = nghCount(G, vDel, cond_fu, clearZeroU, em, no_dense);
			vDel = nghCount(G, uDel, cond_fv, clearV, em, no_dense);
		}

		size_t uCount = pbbslib::reduce_add(sequence<uintE>(n_a, [&](size_t i) {return (D[i]>0);}));

		auto Du =
			sequence<uintE>(n, [&](size_t i) {
				if (i > bipartition || D[i] == 0)
					return std::numeric_limits<uintE>::max();
				return D[i];
			});

		auto abuckets = make_vertex_buckets(n,Du,increasing,num_buckets);
		// makes num_buckets open buckets
		// for each vertex [0, n_a-1], it puts it in bucket D[i]
		auto getUBuckets = [&](const std::tuple<uintE, uintE> &p)
			-> const std::optional<std::tuple<uintE, uintE>> {
			uintE u = std::get<0>(p), edgesRemoved = std::get<1>(p);
			uintE new_deg = std::max(D[u] - edgesRemoved, static_cast<uintE>(max_alpha));
			D[u] = new_deg;
			return wrap(u, abuckets.get_bucket(new_deg));
		};

		pt.stop();
		while (finished != uCount)
		{
			bt.start();
			auto ubkt = abuckets.next_bucket();
			bt.stop();
			max_alpha = std::max(max_alpha, ubkt.id);

			if (ubkt.id == 0)
				continue;
			auto activeU = vertexSubset(n, std::move(ubkt.identifiers));
			finished += activeU.size(); // add to finished set
			par_for(0, activeU.size(), [&](size_t i) {
				size_t index = activeU.vtx(i);
				par_for(1, max_alpha, [&](size_t j) {
					pbbslib::write_max(&BetaMax[index][j],beta);
				});
			});
			
			ft.start();
			vertexSubsetData deleteV = nghCount(G, activeU, cond_fv, clearV, em, no_dense);
			vertexSubsetData movedU = nghCount(G, deleteV, cond_fu, getUBuckets, em, no_dense);
			ft.stop();
			bt.start();
			abuckets.update_buckets(movedU);
			bt.stop();
			rho_beta++;
		}
		abuckets.del();
		em.del();
		return std::pair<size_t,size_t>(rho_beta,max_alpha);
	}

} // namespace gbbs
