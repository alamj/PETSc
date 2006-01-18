#ifndef included_ALE_CoSifter_hh
#define included_ALE_CoSifter_hh

#ifndef  included_ALE_Sifter_hh
#include <Sifter.hh>
#endif
#include <stack>
#include <queue>

// Dmitry's explanation:
//
// Okay, check out what I have put there.
// It's a rather high-level interface, but I think it sketches out the implementation idea.  I have also become a master of switching from 'public' to 'private' and back.

// The idea is to put more power into BiGraphs (BipartiteGraphs).  They are like Sieves but with two point types (source and target) and no recursive operations (nCone, closure, etc).
// I claim they should be parallel, so cone/support completions should be computable for them.  The footprint is incorporated into the color of the new BiGraph, which is returned as a completion.
// It would be very natural to have Sieve<Point_, Color_> to extend BiGraph<Point_, Point_, Color_> with the recursive operations.

// The reason for putting the completion functionality into BiGraphs is that patches and indices under and over a topology Sieve are BiGraphs and have to be completed:
// the new overlap_patches has to encode patch pairs along with the rank of the second patch (first is always local); likewise, overlap_indices must encode a pair of intervals with a rank
// -- the attached to the same Sieve point by two different processes -- one local and one (possibly) remote.  At any rate, the support completion of 'patches' contains all the information
// needed for 'overlap_patches' -- remember that struct with a triple {point, patch, number} you had on the board?   Likewise for 'overlap_indices' built out of the cone completion of 'indices'.

// Once the 'overlap_XXX' are computed, we can allocate the storage for the Delta data and post sends receives.
// We should be able to reuse the completion subroutine from the old Sieve.
// So you are right that perhaps Sieve completion gets us to the CoSieve completion, except I think it's the BiGraph completion this time.
// I can do the completion when I come back if you get the serial BiGraph/Sieve stuff going.
//
namespace ALE {
  namespace def {


    //
    // CoSieve:
    // This object holds the data layout and the data over a Sieve patitioned into support patches.
    //
    template <typename Sieve_, typename Patch_, typename Index_, typename Value_>
    class CoSieve {
    public:
      //     Basic types
      typedef Sieve_ Sieve;
      typedef Value_ value_type;
      typedef Patch_ patch_type;
      typedef Index_ index_type;
      typedef std::vector<index_type> IndexArray;
      typedef std::map<typename Sieve::point_type, index_type> IndexMap;
      int debug;
    private:
      // Base topology
      Obj<Sieve>   _topology;
    public:
      // Breakdown of the base Sieve into patches
      //   the colors (int) order the points (Sieve::point_type) over a patch (patch_type).
      typedef BiGraph<typename Sieve::point_type, patch_type, int> patches_type;
    private:
      patches_type _patches; 
    public:
      // Attachment of fiber dimension intervals to Sieve points
      //   fields are encoded by colors, which double as the field ordering index
      //   colors (<patch_type, int>) order the indices (index_type, usually an interval) over a point (Sieve::point_type).
      typedef std::pair<patch_type, typename Sieve::color_type> index_color;
      typedef BiGraph<index_type, typename Sieve::point_type, index_color> indices_type;
      friend std::ostream& operator<<(std::ostream& os, const index_color& c) {
        os << c.first << ","<< c.second;
        return os;
      };
    private:
      indices_type _indices; 
    private:
      // Holds size and values for each patch
      std::map<patch_type, int>          _storageSize;
      std::map<patch_type, value_type *> _storage;

      void __clear() {
        Obj<typename patches_type::baseSequence> patches = this->_patches.base();

        for(typename patches_type::baseSequence::iterator p_itor = patches->begin(); p_itor != patches->end(); ++p_itor) {
          delete [] this->_storage[*p_itor];
        }
        this->_patches.clear();
        this->_indices.clear();
        this->_storage.clear();
        this->_storageSize.clear();
      };
    public:
      CoSieve() : debug(0) {};
      //     Topology Manipulation
      void           setTopology(const Obj<Sieve>& topology) {this->_topology = topology;};
      Obj<Sieve>     getTopology() {return this->_topology;};
      //     Patch manipulation
    private:
      template <typename InputSequence>
      void order(const patch_type& patch, Obj<InputSequence> base, std::map<typename Sieve::point_type, typename Sieve::point_type>& seen, int& offset) {
        // To enable the depth-first order traversal without recursion, we employ a stack.
        std::stack<typename Sieve::point_type> stk;

        // We traverse the sub-bundle over base
        for(typename InputSequence::reverse_iterator b_ritor = base->rbegin(); b_ritor != base->rend(); ++b_ritor) {
          if (debug) {std::cout << "Init Pushing " << *b_ritor << " on stack" << std::endl;}
          stk.push(*b_ritor);
        }
        while(1) {
          if (stk.empty()) break;

          typename Sieve::point_type p = stk.top(); stk.pop();
          int p_dim = this->getIndexDimension(patch, p);
          int p_off;

          if(seen.find(p) != seen.end()){
            // If p has already been seen, we use the stored offset.
            p_off = seen[p].prefix;
          } else {
            // The offset is only incremented when we encounter a point not yet seen
            p_off   = offset;
            seen[p] = typename Sieve::point_type(p_off, 0);
            offset += p_dim;
          }
          if (debug) {std::cout << "  Point " << p << " with dimension " << p_dim << " and offset " << p_off << std::endl;}

          Obj<typename Sieve::coneSequence> cone = this->getTopology()->cone(p);
          for(typename InputSequence::iterator s_itor = base->begin(); s_itor != base->end(); ++s_itor) {
            // I THINK THIS IS ALWAYS TRUE NOW
            if (*s_itor == p) {
              // If s (aka p) has a nonzero dimension but has not been indexed yet
              if((p_dim > 0) && (seen[p].index == 0)) {
                typename Sieve::point_type newIndex(p_off, p_dim);

                seen[p] = newIndex;
                this->_indices.replaceSourceOfTarget(p, newIndex);
                if (debug) {std::cout << "    Assigned new index " << newIndex << std::endl;}
              }
              if (debug) {std::cout << "  now ordering cone" << std::endl;}
              this->order(patch, cone, seen, offset);
              break;
            }
          }
        }
      };

      void allocateAndOrderPatch(const patch_type& patch) {
        std::map<typename Sieve::point_type, typename Sieve::point_type> seen;
        int                                            offset = 0;

        if (debug) {std::cout << "Ordering patch " << patch << std::endl;}
        this->order(patch, this->getPatch(patch), seen, offset);

        if (this->_storage.find(patch) != this->_storage.end()) {
          delete [] this->_storage[patch];
        }
        if (debug) {std::cout << "Allocated patch " << patch << " of size " << offset << std::endl;}
        this->_storage[patch] = new value_type[offset];
        this->_storageSize[patch] = offset;
      };
    public:
      // This attaches Sieve::point_type points to a patch in the order prescribed by the sequence; 
      // care must be taken not to assign duplicates (or should it?)
      template<typename pointSequence>
      void                            setPatch(const Obj<pointSequence>& points, const patch_type& patch) {
        this->_patches.addCone(points, patch);
        this->_patches.stratify();
      };
      // This retrieves the Sieve::point_type points attached to a given patch
      Obj<typename patches_type::coneSequence> getPatch(const patch_type& patch) {
        return this->_patches.cone(patch);
      };
      //     Index manipulation
      #undef __FUNCT__
      #define __FUNCT__ "CoSieve::orderPatches"
      void orderPatches() {
        ALE_LOG_EVENT_BEGIN;
        Obj<typename patches_type::baseSequence> base = this->_patches.base();

        this->_indices.stratify();
        for(typename patches_type::baseSequence::iterator b_iter = base->begin(); b_iter != base->end(); ++b_iter) {
          this->allocateAndOrderPatch(*b_iter);
        }
        ALE_LOG_EVENT_END;
      };
      // These attach index_type indices of a given color to a Sieve::point_type point
      void  addIndices(const patch_type& patch, const index_type& indx, typename Sieve::color_type color, const typename Sieve::point_type& p) {
        this->_indices.addCone(indx, p, index_color(patch, color));
      };
      template<typename indexInputSequence>
      void  addIndices(const patch_type& patch, const Obj<indexInputSequence>& indices, typename Sieve::color_type color, const typename Sieve::point_type& p) {
        this->_indices.addCone(indices, p, index_color(patch, color));
      };
      void  setIndices(const patch_type& patch, const index_type& indices, typename Sieve::color_type color, const typename Sieve::point_type& p) {
        this->_indices.setCone(indices, p, index_color(patch, color));
      };
      template<typename indexInputSequence>
      void  setIndices(const patch_type& patch, const Obj<indexInputSequence>& indices, typename Sieve::color_type color, const typename Sieve::point_type& p) {
        this->_indices.setCone(indices, p, index_color(patch, color));
      };
      // This retrieves the index_type indices of a given color attached to a Sieve::point_type point
      const index_type& getIndex(const patch_type& patch, const typename Sieve::point_type& p) {
        return *this->_indices.cone(p)->begin();
      };
      Obj<typename indices_type::coneSequence> getIndices(const patch_type& patch, const typename Sieve::point_type& p) {
        return this->_indices.cone(p);
      };
      template<typename pointSequence>
      Obj<IndexMap> getIndices(const patch_type& patch, Obj<pointSequence> points) {
        Obj<IndexMap> indices = IndexMap();

        for(typename pointSequence::iterator p_iter = points->begin(); p_iter != points->end(); ++p_iter) {
          Obj<typename indices_type::coneSequence> ind = this->getIndices(patch, *p_iter);

          if (ind->begin() != ind->end()) {
            (*indices)[*p_iter] = *ind->begin();
            if (debug) {std::cout << "Got indices " << (*indices)[*p_iter] << " for " << *p_iter << std::endl;}
          }
        }
        return indices;
      }
    private:
      template<typename orderSequence>
      std::map<int, typename Sieve::point_type> __checkOrderChain(Obj<orderSequence> order, int& minDepth, int& maxDepth) {
        Obj<Sieve> topology = this->getTopology();
        std::map<int, typename Sieve::point_type> dElement;
        minDepth = 0;
        maxDepth = 0;

        // A topology cell-tuple contains one element per dimension, so we order the points by depth.
        for(typename orderSequence::iterator ord_itor = order->begin(); ord_itor != order->end(); ord_itor++) {
          int depth = topology->depth(*ord_itor);

          if (depth < 0) {
            throw Exception("Invalid element: negative depth returned"); 
          }
          if (depth > maxDepth) {
            maxDepth = depth;
          }
          if (depth < minDepth) {
            minDepth = depth;
          }
          dElement[depth] = *ord_itor;
        }
        // Verify that the chain is a "baricentric chain", i.e. it starts at depth 0
        //   and has an element of every depth between 0 and maxDepth
        //   and that each element at depth d is in the cone of the element at depth d+1
        if(minDepth != 0) {
          throw Exception("Invalid order chain: minimal depth is nonzero");
        }
        for(int d = 0; d <= maxDepth; d++) {
          typename std::map<int, typename Sieve::point_type>::iterator d_itor = dElement.find(d);

          if(d_itor == dElement.end()){
            ostringstream ex;
            //FIX: ex << "[" << this->getCommRank() << "]: " << "Missing Point at depth " << d;
            ex << "Missing Point at depth " << d;
            throw ALE::Exception(ex.str().c_str());
          }
          if(d > 0) {
            if(!topology->coneContains(dElement[d], dElement[d-1])){
              ostringstream ex;
              // FIX: ex << "[" << this->getCommRank() << "]: ";
              ex << "point (" << dElement[d-1].prefix << ", " << dElement[d-1].index << ") at depth " << d-1 << " not in the cone of ";
              ex << "point (" << dElement[d].prefix << ", " << dElement[d].index << ") at depth " << d;
              throw ALE::Exception(ex.str().c_str());
            }
          }
        }
        return dElement;
      };
      void __orderElement(int dim, typename Sieve::point_type element, std::map<int, std::queue<index_type> >& ordered, ALE::Obj<ALE::def::PointSet> elementsOrdered) {
        if (elementsOrdered->find(element) != elementsOrdered->end()) return;
        ordered[dim].push(element);
        elementsOrdered->insert(element);
        if (debug) {std::cout << "  ordered element " << element << " dim " << dim << std::endl;}
      };
      typename Sieve::point_type __orderCell(int dim, std::map<int, typename Sieve::point_type>& orderChain, std::map<int, std::queue<index_type> >& ordered, Obj<PointSet> elementsOrdered) {
        typename Sieve::point_type last;

        if (debug) {
          std::cout << "Ordering cell " << orderChain[dim] << " dim " << dim << std::endl;
          for(int d = 0; d < dim; d++) {
            std::cout << "  orderChain["<<d<<"] " << orderChain[d] << std::endl;
          }
        }
        if (dim == 0) {
          last = orderChain[0];
          this->__orderElement(0, last, ordered, elementsOrdered);
          return last;
        } else if (dim == 1) {
          Obj<typename Sieve::coneSequence> flip = this->_topology->cone(orderChain[1]);
          bool found = false;

          if (flip->size() != 2) throw ALE::Exception("Last 1d edge did not separate two faces");
          for(typename Sieve::coneSequence::iterator c_iter = flip->begin(); c_iter != flip->end(); ++c_iter) {
            if (*c_iter != orderChain[dim-1]) {
              last = *c_iter;
              found = true;
              break;
            }
          }
          if (!found) throw ALE::Exception("Inconsistent edge separation");
          this->__orderElement(0, orderChain[0], ordered, elementsOrdered);
          this->__orderElement(0, last, ordered, elementsOrdered);
          this->__orderElement(1, orderChain[1], ordered, elementsOrdered);
          orderChain[dim-1] = last;
          return last;
        }
        Obj<Sieve> closure = this->_topology->closureSieve(orderChain[dim]);
        do {
          last = this->__orderCell(dim-1, orderChain, ordered, elementsOrdered);
          if (debug) {std::cout << "    last " << last << std::endl;}
          //FIX: We could make a support() which is relative to a closure instead of using closureSieve()
          Obj<typename Sieve::supportSequence> faces = closure->support(last);
          bool found = false;

          if (faces->size() != 2) {
            //std::cout << "Closure:" << std::endl << closure;
            std::cout << "Closure:" << std::endl;
            Obj<typename Sieve::baseSequence> base = closure->base();

            for(typename Sieve::baseSequence::iterator b_iter = base->begin(); b_iter != base->end(); ++b_iter) {
              Obj<typename Sieve::coneSequence> cone = closure->cone(*b_iter);

              std::cout << "Base point " << *b_iter << " with cone:" << std::endl;
              for(typename Sieve::coneSequence::iterator c_iter = cone->begin(); c_iter != cone->end(); ++c_iter) {
                std::cout << "  " << *c_iter << std::endl;
              }
            }
            for(typename Sieve::supportSequence::iterator s_iter = faces->begin(); s_iter != faces->end(); ++s_iter) {
              std::cout << "    support point " << *s_iter << std::endl;
            }
            throw ALE::Exception("Last edge did not separate two faces");
          }
          for(typename Sieve::supportSequence::iterator s_iter = faces->begin(); s_iter != faces->end(); ++s_iter) {
            if (*s_iter != orderChain[dim-1]) {
              last = orderChain[dim-1];
              orderChain[dim-1] = *s_iter;
              found = true;
              break;
            }
          }
          if (!found) throw ALE::Exception("Inconsistent edge separation");
        } while(elementsOrdered->find(orderChain[dim-1]) == elementsOrdered->end());
        if (debug) {
          std::cout << "Finish ordering for cell " << orderChain[dim] << std::endl;
          std::cout << "  with last " << last << std::endl;
        }
        orderChain[dim-1] = last;
        this->__orderElement(dim, orderChain[dim], ordered, elementsOrdered);
        return last;
      };
    public:
      template<typename orderSequence>
      Obj<IndexArray> getOrderedIndices(const patch_type& patch, Obj<orderSequence> order) {
        // We store the elements ordered in each dimension
        std::map<int, std::queue<typename Sieve::point_type> > ordered;
        // Set of the elements already ordered
        Obj<PointSet> elementsOrdered = PointSet();
        Obj<IndexArray> indexArray = IndexArray();
        int minDepth, maxDepth;

        std::map<int, typename Sieve::point_type> dElement = this->__checkOrderChain(order, minDepth, maxDepth);
        if (debug) {std::cout << "Ordering " << dElement[maxDepth] << std::endl;}
        // Could share the closure between these methods
        Obj<IndexMap> indices = this->getIndices(patch, this->_topology->closure(dElement[maxDepth]));
        typename Sieve::point_type last = this->__orderCell(maxDepth, dElement, ordered, elementsOrdered);
        for(int d = minDepth; d <= maxDepth; d++) {
          while(!ordered[d].empty()) {
            index_type ind = (*indices)[ordered[d].front()];

            ordered[d].pop();
            if (debug) {std::cout << "  indices " << ind << std::endl;}
            if (ind.index > 0) {
              indexArray->push_back(ind);
            }
          }
        }
        return indexArray;
      };
      int getIndexDimension(const patch_type& patch) {
        return this->_storageSize[patch];
      }
      int getIndexDimension(const patch_type& patch, const typename Sieve::point_type& p) {
        Obj<typename indices_type::coneSequence> cone = this->_indices.cone(p);
        int dim = 0;

        for(typename indices_type::coneSequence::iterator iter = cone->begin(); iter != cone->end(); ++iter) {
          dim += (*iter).index;
        }
        if (debug) {std::cout << "  getting dimension " << dim << " of " << p << " in patch " << patch << std::endl;}
        return dim;
      };
      int getIndexDimension(const patch_type& patch, const typename Sieve::point_type& p, typename Sieve::color_type color) {
        Obj<typename indices_type::coneSequence> cone = this->_indices.cone(p, index_color(patch, color));
        int dim = 0;

        for(typename indices_type::coneSequence::iterator iter = cone->begin(); iter != cone->end(); ++iter) {
          dim += (*iter).index;
        }
        if (debug) {std::cout << "  getting dimension " << dim << " of " << p << "(" << color << ") in patch " << patch << std::endl;}
        return dim;
      };
      void setIndexDimension(const patch_type& patch, const typename Sieve::point_type& p, int indexDim) {
        this->setIndexDimension(patch, p, typename Sieve::color_type(), indexDim);
      }
      void setIndexDimension(const patch_type& patch, const typename Sieve::point_type& p, typename Sieve::color_type color, int indexDim) {
        this->setIndices(patch, typename Sieve::point_type(-1, indexDim), color, p);
      }
      // Attach indexDim indices to each element of a certain depth in the topology
      void setIndexDimensionByDepth(int depth, int indexDim) {
        this->setIndexDimensionByDepth(depth, typename Sieve::color_type(), indexDim);
      }
      #undef __FUNCT__
      #define __FUNCT__ "CoSieve::setIndexDimensionByDepth"
      void setIndexDimensionByDepth(int depth, typename Sieve::color_type color, int indexDim) {
        ALE_LOG_EVENT_BEGIN;
        Obj<typename patches_type::baseSequence> base = this->_patches.base();
        Obj<typename Sieve::depthSequence> stratum = this->getTopology()->depthStratum(depth);

        if (debug) {std::cout << "Setting all points of depth " << depth << " to have dimension " << indexDim << std::endl;}
        for(typename patches_type::baseSequence::iterator b_iter = base->begin(); b_iter != base->end(); ++b_iter) {
          if (debug) {std::cout << "  traversing patch " << *b_iter << std::endl;}
          for(typename Sieve::depthSequence::iterator iter = stratum->begin(); iter != stratum->end(); ++iter) {
            if (debug) {std::cout << "  setting dimension of " << *iter << " to " << indexDim << std::endl;}
            this->setIndexDimension(*b_iter, *iter, color, indexDim);
          }
        }
        ALE_LOG_EVENT_END;
      };
      const value_type *restrict(const patch_type& patch) {
        return this->_storage[patch];
      };
      const value_type *restrict(const patch_type& patch, const typename Sieve::point_type& p) {
        Obj<typename indices_type::coneSequence> indices = this->getIndices(patch, p);

        if (indices->size() == 1) {
          return &(this->_storage[patch][(*indices->begin()).prefix]);
        } else {
          static value_type *values     = NULL;
          static int         valuesSize = -1;
          int                size = 0;
          int                i = 0;

          for(typename indices_type::coneSequence::iterator i_iter = indices->begin(); i_iter != indices->end(); ++i_iter) {
            size += (*i_iter).index;
          }
          if (size != valuesSize) {
            if (values) delete [] values;
            values = new value_type[size];
          }
          for(typename indices_type::coneSequence::iterator i_iter = indices->begin(); i_iter != indices->end(); ++i_iter) {
            for(int ind = (*i_iter).prefix; ind < (*i_iter).prefix + (*i_iter).index; ind++) {
              values[i++] = ind;
            }
          }
          return values;
        }
      }
      template<typename InputSequence>
      const value_type *restrict(const patch_type& patch, const InputSequence& pointSequence) {
        throw ALE::Exception("Not implemented");
      };
      // Insert values into the specified patch
      void update(const patch_type& patch, const typename Sieve::point_type& p, value_type values[]) {
        Obj<typename indices_type::coneSequence> indices = this->getIndices(patch, p);

        for(typename indices_type::coneSequence::iterator ind = indices->begin(); ind != indices->end(); ++ind) {
          int offset = (*ind).prefix;

          for(int i = 0; i < (*ind).index; ++i) {
            if (debug) {std::cout << "Set a[" << offset+i << "] = " << values[i] << " on patch " << patch << std::endl;}
            this->_storage[patch][offset+i] = values[i];
          }
        }
      }
      template<typename InputSequence>
      void update(const patch_type& patch, const InputSequence& pointSequence, value_type values[]) {
        throw ALE::Exception("Not implemented");
      };
    public:
      //      Reduction types
      //   The overlap types must be defined in terms of the patch_type and index types;
      // they serve as Patch_ and Index_ for the delta CoSieve;
      // there may be a better way than encoding then as nested std::pairs;
      // the int member encodes the rank that contributed the second member of the inner pair (patch or index).
      typedef std::pair<std::pair<patch_type, patch_type>, int> overlap_patch_type;
      typedef std::pair<std::pair<index_type, index_type>, int> overlap_index_type;
      //   The delta CoSieve uses the overlap_patch_type and the overlap_index_type;  it should be impossible to change
      // the structure of a delta CoSieve -- only queries are allowed, so it is a sort of const_CoSieve
      typedef CoSieve<Sieve, overlap_patch_type, overlap_index_type, value_type> delta_type;

      //      Reduction methods (by stage)
      // Compute the overlap patches and indices that determine the structure of the delta CoSieve
      Obj<overlap_patch_type> computeOverlapPatches();  // use support completion on _patches and reorganize (?)
      Obj<overlap_index_type> computeOverlapIndices();  // use cone completion on    _indices and reorganize (?)
      // Compute the delta CoSieve
      Obj<delta_type>           computeDelta();           // compute overlap patches and overlap indices and use them
      // Reduce the CoSieve by computing the delta first and then fixing up the data on the overlap;
      // note: a 'zero' delta corresponds to the identical data attached from either side of each overlap patch
      // To implement a given policy, override this method and utilize the result of computeDelta.
      void                      reduce();
    }; // class CoSifter
  } // namespace def
} // namespace ALE

#endif
