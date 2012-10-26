#pragma once

#include "Evaluator.hpp"

#include "L2L.hpp"
#include "L2P.hpp"

template <typename Tree, typename Kernel>
class EvalDownward : public Evaluator<EvalDownward<Tree,Kernel>>
{
  const Tree& tree;
  const Kernel& K;

public:
  EvalDownward(const Tree& t, const Kernel& k)
  : tree(t), K(k) {
  }

  template <typename BoxContext>
  void execute(BoxContext& bc) const {
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
          // If not leaf, make L2L calls
	  L2L::eval(K, bc, box);
        }
      }
    }
  }
};

template <typename Tree, typename Kernel, typename Options>
EvalDownward<Tree,Kernel>* make_downward(const Tree& tree,
					 const Kernel& kernel,
					 const Options&) {
  return new EvalDownward<Tree,Kernel>(tree, kernel);
}
