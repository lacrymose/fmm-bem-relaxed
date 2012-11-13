#pragma once

#include "Evaluator.hpp"

#include "L2L.hpp"
#include "L2P.hpp"

struct EvalDownward : public Evaluator<EvalDownward>
{
  template <typename BoxContext>
  void execute(BoxContext& bc) const {
    auto tree = bc.target_tree();
    auto K = bc.kernel();

    // For the highest level down to the lowest level
    for (unsigned l = 1; l < tree.levels(); ++l) {
      // For all boxes at this level
      auto b_end = tree.box_end(l);
      for (auto bit = tree.box_begin(l); bit != b_end; ++bit) {
        auto box = *bit;

        // Initialize box data
        if (box.is_leaf()) {
          // If leaf, make L2P calls
	  L2P::eval(K, bc, box);
        } else {
	  // If not leaf, then for all the children L2L
	  auto c_end = box.child_end();
	  for (auto cit = box.child_begin(); cit != c_end; ++cit)
	    L2L::eval(K, bc, box, *cit);
        }
      }
    }
  }
};
