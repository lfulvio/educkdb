// See https://raw.githubusercontent.com/duckdb/duckdb/main/LICENSE for licensing information

#include "duckdb.hpp"
#include "duckdb-internal.hpp"
#ifndef DUCKDB_AMALGAMATION
#error header mismatch
#endif


// LICENSE_CHANGE_BEGIN
// The following code up to LICENSE_CHANGE_END is subject to THIRD PARTY LICENSE #20
// See the end of this file for a list

//
//  SkipList.cpp
//  SkipList
//
//  Created by Paul Ross on 19/12/2015.
//  Copyright (c) 2017 Paul Ross. All rights reserved.
//

#include <cstdlib>
#ifdef SKIPLIST_THREAD_SUPPORT
#include <mutex>
#endif
#include <string>



// LICENSE_CHANGE_BEGIN
// The following code up to LICENSE_CHANGE_END is subject to THIRD PARTY LICENSE #20
// See the end of this file for a list

#ifndef __SkipList__SkipList__
#define __SkipList__SkipList__

/**
 * @file
 *
 * Project: skiplist
 *
 * Created by Paul Ross on 15/11/2015.
 *
 * Copyright (c) 2015-2023 Paul Ross. All rights reserved.
 *
 * @code
 * MIT License
 *
 * Copyright (c) 2017-2023 Paul Ross
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * @endcode
 */

/** @mainpage
 *
 * General
 * =======
 * This is a generic skip list implementation for any type T.
 * There only restriction on the size of this skip list is the available memory.
 *
 * A skip list is a singly linked list of ordered nodes with a series of other, coarser, lists that reference a subset
 * of nodes in order.
 * 'Level' is an size_t that specifies the coarseness of the linked list, level 0 is the linked list to every node.
 *
 * Typically:
 * - The list at level 1 links (ideally) to every other node.
 * - The list at level 2 links (ideally) to every fourth node and so on.
 *
 * In general the list at level n links (ideally) to every 2**n node.
 *
 * These additional lists allow rapid location, insertion and removal of nodes.
 * These lists are created and updated in a probabilistic manner and this is achieved at node creation time by tossing a
 * virtual coin.
 * These lists are not explicit, they are implied by the references between Nodes at a particular level.
 *
 * Skip lists are alternatives to balanced trees for operations such as a rolling median.
 * The disadvantages of skip lists are:
    - Less space efficient than balanced trees (see 'Space Complexity' below).
    - performance is similar to balanced trees except finding the mid-point which is @c O(log(N)) for a skip list
        compared with @c O(1) for a balanced tree.
 *
 * The advantages claimed for skip lists are:
    - The insert() and remove() logic is simpler (I do not subscribe to this).
 *
 * Examples of Usage
 * =================
 *
 * C++
 * ---
 * @code
 * #include "SkipList.h"
 *
 * OrderedStructs::SkipList::HeadNode<double> sl;
 *
 * sl.insert(42.0);
 * sl.insert(21.0);
 * sl.insert(84.0);
 * sl.has(42.0) // true
 * sl.size() // 3
 * sl.at(1) // 42.0
 * @endcode
 *
 * Python
 * ------
 * @code
 * import orderedstructs
 *
 * sl = orderedstructs.SkipList(float)
 * sl.insert(42.0)
 * sl.insert(21.0)
 * sl.insert(84.0)
 * sl.has(42.0) # True
 * sl.size() # 3
 * sl.at(1) # 42.0
 * @endcode
 *
 * Design
 * ======
 *
 * This skip list design has the coarser lists implemented as optional additional links between the nodes themselves.
 * The drawing below shows a well formed skip list with a head node ('HED') linked to the ordered nodes A to H.
 *
 * @code
 *
 | 5 E |------------------------------------->| 4 0 |---------------------------->| NULL |
 | 1 A |->| 2 C |---------->| 2 E |---------->| 2 G |---------->| 2 0 |---------->| NULL |
 | 1 A |->| 1 B |->| 1 C |->| 1 D |->| 1 E |->| 1 F |->| 1 G |->| 1 H |->| 1 0 |->| NULL |
 | HED |  |  A  |  |  B  |  |  C  |  |  D  |  |  E  |  |  F  |  |  G  |  |  H  |
 * @endcode
 *
 * Each node has a stack of values that consist of a 'width' and a reference to another node (or NULL).
 * At the lowest level is a singly linked list and all widths are 1.
 * At level 1 the links are (ideally) to every other node and at level 2 the links are (ideally) to every fourth node.
 * The 'widths' at each node/level specify how many level 0 nodes the node reference skips over.
 * The widths are used to rapidly index into the skip list starting from the highest level and working down.
 *
 * To understand how the skip list is maintained, consider insertion; before inserting node 'E' the skip list would look
 * like this:
 *
 * @code
 *
 | 1 A |->| 2 C |---------->| 3 G |------------------->| 2 0 |---------->| NULL |
 | 1 A |->| 1 B |->| 1 C |->| 1 D |->| 1 F |->| 1 G |->| 1 H |->| 1 0 |->| NULL |
 | HED |  |  A  |  |  B  |  |  C  |  |  D  |  |  F  |  |  G  |  |  H  |
 *
 * @endcode
 *
 * Inserting 'E' means:
 * - Finding where 'E' should be inserted (after 'D').
 * - Creating node 'E' with a random height (heads/heads/tails so 3 high).
 * - Updating 'D' to refer to 'E' at level 0.
 * - Updating 'C' to refer to 'E' at level 1 and decreasing C's width to 2, increasing 'E' width at level 1 to 2.
 * - Expanding HED to level 2 with a reference to 'E' and a width of 5.
 * - Updating 'E' with a reference to NULL and a width of 4.
 *
 * Recursive Search for the Node Position
 * --------------------------------------
 * The first two operations are done by a recursive search.
 * This creates the chain HED[1], A[1], C[1], C[0], D[0] thus E will be created at level 0 and inserted after D.
 *
 * Node Creation
 * -------------
 * Node E is created with a stack containing a single pointer to the next node F.
 * Then a virtual coin is tossed, for each 'head' and extra NULL reference is added to the stack.
 * If a 'tail' is thrown the stack is complete.
 * In the example above when creating Node E we have encountered tosses of 'head', 'head', 'tail'.
 *
 * Recursive Unwinding
 * -------------------
 * The remaining operations are done as recursion unwinds:
 *
 * - D[0] and C[0] update E[1] with their cumulative width (2).
 * - C[1] adds 1 to width (a new node is inserted) then subtracts E[1].
 * - Then C[1]/E[1] are swapped so that the pointers and widths are correct.
 * - And so on until HED is reached, in this case a new level is added and HED[2] swapped with E[2].
 *
 * A similar procedure will be followed, in reverse, when removing E to restore the state of the skip list to the
 * picture above.
 *
 * Algorithms
 * ==========
 * There doesn't seem to be much literature that I could find about the algorithms used for a skip list so these have
 * all been invented here.
 *
 * In these descriptions:
 *
 * - 'right' is used to mean move to a higher ordinal node.
 * - 'left' means to move to a lower ordinal node.
 * - 'up' means to move to a coarser grained list, 'top' is the highest.
 * - 'down' means to move to a finer grained list, 'bottom' is the level 0.
 *
 * has(T &val) const;
 * ------------------
 * This returns true/false is the skip list has the value val.
 * Starting at the highest possible level search rightwards until a larger value is encountered, then drop down.
 * At level 0 return true if the Node value is the supplied value.
 * This is @c O(log(N)) for well formed skip lists.
 *
 * at(size_t index) const;
 * -----------------------
 * This returns the value of type T at the given index.
 * The algorithm is similar to has(T &val) but the search moves rightwards if the width is less than the index and
 * decrementing the index by the width.
 *
 * If progress can not be made to the right, drop down a level.
 * If the index is 0 return the node value.
 * This is @c O(log(N)) for well formed skip lists.
 *
 * insert(T &val)
 * --------------
 * Finding the place to insert a node follows the has(T &val) algorithm to find the place in the skip list to create a
 * new node.
 * A duplicate value is inserted after any existing duplicate values.
 *
 * - All nodes are inserted at level 0 even if the insertion point can be seen at a higher level.
 * - The search for an insertion location creates a recursion stack that, when unwound, updates the traversed nodes
 *      <tt>{width, Node<T>*}</tt> data.
 * - Once an insert position is found a Node is created whose height is determined by repeatedly tossing a virtual coin
 *      until a 'tails' is thrown.
 * - This node initially has all node references to be to itself (this), and the widths set to 1 for level 0 and 0 for
 *      the remaining levels, they will be used to sum the widths at one level down.
 * - On recursion ('left') each node adds its width to the new node at the level above the current level.
 * - On moving up a level the current node swaps its width and node pointer with the new node at that new level.
 *
 * remove(T &val)
 * --------------
 *
 * If there are duplicate values the last one is removed first, this is for symmetry with insert().
 * Essentially this is the same as insert() but once the node is found the insert() updating algorithm is reversed and
 * the node deleted.
 *
 * Code Layout
 * ===========
 * There are three classes defined in their own .h files and these are all included into the SkipList.h file.
 *
 * The classes are:
 *
 * <tt>SwappableNodeRefStack</tt>
 *
 * This is simple bookkeeping class that has a vector of <tt>[{skip_width, Node<T>*}, ...]</tt>.
 * This vector can be expanded or contracted at will.
 * Both HeadNode and Node classes have one of these to manage their references.
 *
 * <tt>Node</tt>
 *
 * This represents a single value in the skip list.
 * The height of a Node is determined at construction by tossing a virtual coin, this determines how many coarser
 * lists this node participates in.
 * A Node has a SwappableNodeRefStack object and a value of type T.
 *
 * <tt>HeadNode</tt>
 *
 * There is one of these per skip list and this provides the API to the entire skip list.
 * The height of the HeadNode expands and contracts as required when Nodes are inserted or removed (it is the height
 * of the highest Node).
 * A HeadNode has a SwappableNodeRefStack object and an independently maintained count of the number of Node objects
 * in the skip list.
 *
 * A Node and HeadNode have specialised methods such as has(), at(), insert(), remove() that traverse the skip lis
 * recursively.
 *
 * Other Files of Significance
 * ---------------------------
 * SkipList.cpp exposes the random number generator (rand()) and seeder (srand()) so that they can be accessed
 * CPython for deterministic testing.
 *
 * cSkipList.h and cSkipList.cpp contains a CPython module with a SkipList implementation for a number of builtin
 * Python types.
 *
 * IntegrityEnums.h has definitions of error codes that can be created by the skip list integrity checking functions.
 *
 * Code Idioms
 * ===========
 *
 * Prevent Copying
 * ---------------
 * Copying operations are (mostly) prohibited for performance reasons.
 * The only class that allows copying is struct NodeRef that contains fundamental types.
 * All other classes declare their copying operation private and unimplemented (rather than using C++11 delete) for
 * compatibility with older compilers.
 *
 * Reverse Loop of Unsigned int
 * ----------------------------
 * In a lot of the code we have to count down from some value to 0
 * with a size_t (an unsigned integer type) The idiom used is this:
 *
 * @code
 *
 *  for (size_t l = height(); l-- > 0;) {
 *      // ...
 *  }
 *
 * @endcode
 *
 * The "l-- > 0" means test l against 0 then decrement it.
 * l will thus start at the value height() - 1 down to 0 then exit the loop.
 *
 * @note If l is declared before the loop it will have the maximum value of a size_t unless a break statement is
 * encountered.
 *
 * Roads not Travelled
 * ===================
 * Certain designs were not explored, here they are and why.
 *
 * Key/Value Implementation
 * ------------------------
 * Skip lists are commonly used for key/value dictionaries. Given things
 * like map<T> or unorderedmap<T> I see no reason why a SkipList should be used
 * as an alternative.
 *
 * Adversarial Users
 * -----------------
 * If the user knows the behaviour of the random number generator it is possible that they can change the order of
 * insertion to create a poor distribution of nodes which will make operations tend to O(N) rather than O(log(N)).
 *
 * Probability != 0.5
 * ------------------
 * This implementation uses a fair coin to decide the height of the node.
 *
 * Some literature suggests other values such as p = 0.25 might be more efficient.
 * Some experiments seem to show that this is the case with this implementation.
 * Here are some results when using a vector of 1 million doubles and a sliding window of 101 where each value is
 * inserted and removed and the cental value recovered:
 *
 * @code
 *
    Probability calculation        p    Time compared to p = 0.5
     rand() < RAND_MAX / 16;    0.0625   90%
     rand() < RAND_MAX / 8;     0.125    83%
     rand() < RAND_MAX / 4;     0.25     80%
     rand() < RAND_MAX / 2;     0.5     100%
     rand() > RAND_MAX / 4;     0.75    143%
     rand() > RAND_MAX / 8;     0.875   201%
 *
 * @endcode
 *
 * Optimisation: Re-index Nodes on Complete Traversal
 * --------------------------------------------------
 *
 * @todo Re-index Nodes on Complete Traversal ???
 *
 * Optimisation: Reuse removed nodes for insert()
 * ----------------------------------------------
 * @todo Reuse removed nodes for insert() ???
 *
 * Reference Counting
 * ------------------
 * Some time (and particularly space) improvement could be obtained by reference counting nodes so that duplicate
 * values could be eliminated.
 * Since the primary use case for this skip list is for computing the rolling median of doubles the chances of
 * duplicates are slim.
 * For int, long and string there is a higher probability so reference counting might be implemented in the future if
 * these types become commonly used.
 *
 * Use and Array of <tt>{skip_width, Node<T>*}</tt> rather than a vector
 * ----------------------------------------------------------------------
 *
 * Less space would be used for each Node if the SwappableNodeRefStack used a dynamically allocated array of
 * <tt>[{skip_width, Node<T>*}, ...]</tt> rather than a vector.
 *
 * Performance
 * ===========
 *
 * Reference platform: Macbook Pro, 13" running OS X 10.9.5. LLVM version 6.0 targeting x86_64-apple-darwin13.4.0
 * Compiled with -Os (small fast).
 *
 * Performance of at() and has()
 * -----------------------------
 *
 * Performance is O(log(N)) where N is the position in the skip list.
 *
 * On the reference platform this tests as t = 200 log2(N) in nanoseconds for skip lists of doubles.
 * This factor of 200 can be between 70 and 500 for the same data but different indices because of the probabilistic
 * nature of a skip list.
 * For example finding the mid value of 1M doubles takes 3 to 4 microseconds.
 *
 * @note
 * On Linux RHEL5 with -O3 this is much faster with t = 12 log2(N)
 * [main.cpp perf_at_in_one_million(), main.cpp perf_has_in_one_million()]
 *
 * Performance of insert() and remove()
 * ------------------------------------
 * A test that inserts then removes a single value in an empty list takes 440 nanoseconds (around 2.3 million per
 * second).
 * This should be fast as the search space is small.
 *
 * @note
 * Linux RHEL5 with -O3 this is 4.2 million per second. [main.cpp perf_single_insert_remove()]
 *
 * A test that inserts 1M doubles into a skip list (no removal) takes 0.9 seconds (around 1.1 million per second).
 *
 * @note
 * Linux RHEL5 with -O3 this is similar. [main.cpp perf_large_skiplist_ins_only()]
 *
 * A test that inserts 1M doubles into a skip list then removes all of them takes 1.0 seconds (around 1 million per second).
 *
 * @note
 * Linux RHEL5 with -O3 this is similar. [main.cpp perf_large_skiplist_ins_rem()]
 *
 * A test that creates a skip list of 1M doubles then times how long it takes to insert and remove a value at the
 * mid-point takes 1.03 microseconds per item (around 1 million per second).
 *
 * @note
 * Linux RHEL5 with -O3 this is around 0.8 million per second. [main.cpp perf_single_ins_rem_middle()]
 *
 * A test that creates a skip list of 1M doubles then times how long it takes to insert a value, find the value at the
 * mid point then remove that value (using insert()/at()/remove()) takes 1.2 microseconds per item (around 0.84 million
 * per second).
 *
 * @note
 * Linux RHEL5 with -O3 this is around 0.7 million per second. [main.cpp perf_single_ins_at_rem_middle()]
 *
 * Performance of a rolling median
 * -------------------------------
 * On the reference platform a rolling median (using insert()/at()/remove()) on 1M random values takes about 0.93
 * seconds.
 *
 * @note
 * Linux RHEL5 with -O3 this is about 0.7 seconds.
 * [main.cpp perf_1m_median_values(), main.cpp perf_1m_medians_1000_vectors(), main.cpp perf_simulate_real_use()]
 *
 * The window size makes little difference, a rolling median on 1m items with a window size of 1 takes 0.491 seconds,
 * with a window size of 524288 it takes 1.03 seconds.
 *
 * @note
 * Linux RHEL5 with -O3 this is about 0.5 seconds. [main.cpp perf_roll_med_odd_index_wins()]
 *
 * Space Complexity
 * ----------------
 * Given:
 *
 * - t = sizeof(T) ~ typ. 8 bytes for a double
 * - v = sizeof(std::vector<struct NodeRef<T>>) ~ typ. 32 bytes
 * - p = sizeof(Node<T>*) ~ typ. 8 bytes
 * - e = sizeof(struct NodeRef<T>) ~ typ. 8 + p = 16 bytes
 *
 * Then each node: is t + v bytes.
 *
 * Linked list at level 0 is e bytes per node.
 *
 * Linked list at level 1 is, typically, e / 2 bytes per node and so on.
 *
 * So the totality of linked lists is about 2e bytes per node.
 *
 * The total is N * (t + v + 2 * e) which for T as a double is typically 72 bytes per item.
 *
 * In practice this has been measured on the reference platform as a bit larger at 86.0 Mb for 1024*1024 doubles.
 *
 ***************** END: SkipList Documentation *****************/

/// Defined if you want the SkipList to have methods that can output
/// to stream (for debugging for example).
/// Defining this will mean that classes grow methods that use streams.
/// Undef this if you want a smaller binary in production as using streams
/// adds typically around 30kb to the binary.
/// However you may loose useful information such as formatted
/// exception messages with extra data.
//#define INCLUDE_METHODS_THAT_USE_STREAMS
#undef INCLUDE_METHODS_THAT_USE_STREAMS

#include <functional>
#include <vector>
#include <set> // Used for HeadNode::_lacksIntegrityNodeReferencesNotInList()
#include <string> // Used for class Exception


#ifdef DEBUG
#include <cassert>
#else
#ifndef assert
#define assert(x)
#endif
#endif // DEBUG

#ifdef INCLUDE_METHODS_THAT_USE_STREAMS

#include <iostream>
#include <sstream>

#endif // INCLUDE_METHODS_THAT_USE_STREAMS

//#define SKIPLIST_THREAD_SUPPORT
//#define SKIPLIST_THREAD_SUPPORT_TRACE

#ifdef SKIPLIST_THREAD_SUPPORT
#ifdef SKIPLIST_THREAD_SUPPORT_TRACE
#include <thread>
#endif
#include <mutex>
#endif

/**
 * @brief Namespace for all the C++ ordered structures.
 */
namespace duckdb_skiplistlib {
    /**
     * @brief Namespace for the C++ Slip List.
     */
    namespace skip_list {

/************************ Exceptions ****************************/

/**
 * @brief Base exception class for all exceptions in the OrderedStructs::SkipList namespace.
 */
        class Exception : public std::exception {
        public:
            explicit Exception(const std::string &in_msg) : msg(in_msg) {}

            const std::string &message() const { return msg; }

            virtual ~Exception() noexcept {}

        protected:
            std::string msg;
        };

/**
 * @brief Specialised exception case for an index out of range error.
 */
        class IndexError : public Exception {
        public:
            explicit IndexError(const std::string &in_msg) : Exception(in_msg) {}
        };

/**
 * @brief Specialised exception for an value error where the given value does not exist in the Skip List.
 */
        class ValueError : public Exception {
        public:
            explicit ValueError(const std::string &in_msg) : Exception(in_msg) {}
        };

/** @brief Specialised exception used for NaN detection where value != value (example NaNs). */
        class FailedComparison : public Exception {
        public:
            explicit FailedComparison(const std::string &in_msg) : Exception(in_msg) {}
        };

/**
 * This throws an IndexError when the index value >= the size of Skip List.
 * If @ref INCLUDE_METHODS_THAT_USE_STREAMS is defined then the error will have an informative message.
 *
 * @param index The out of range index.
 */
        void _throw_exceeds_size(size_t index);

/************************ END: Exceptions ****************************/

#ifdef SKIPLIST_THREAD_SUPPORT
        /**
         * Mutex used in a multi-threaded environment.
         */
        extern std::mutex gSkipListMutex;
#endif



// LICENSE_CHANGE_BEGIN
// The following code up to LICENSE_CHANGE_END is subject to THIRD PARTY LICENSE #20
// See the end of this file for a list

//
//  NodeRefs.h
//  SkipList
//
//  Created by Paul Ross on 03/12/2015.
//  Copyright (c) 2017 Paul Ross. All rights reserved.
//

#ifndef SkipList_NodeRefs_h
#define SkipList_NodeRefs_h



// LICENSE_CHANGE_BEGIN
// The following code up to LICENSE_CHANGE_END is subject to THIRD PARTY LICENSE #20
// See the end of this file for a list

#ifndef SkipList_IntegrityEnums_h
#define SkipList_IntegrityEnums_h

/**
 * @file
 *
 * Project: skiplist
 *
 * Integrity codes for structures in this code.
 *
 * Created by Paul Ross on 11/12/2015.
 *
 * Copyright (c) 2015-2023 Paul Ross. All rights reserved.
 *
 * @code
 * MIT License
 *
 * Copyright (c) 2015-2023 Paul Ross
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * @endcode
 */

/**
 * Various integrity codes for structures in this code.
 */
enum IntegrityCheck {
    INTEGRITY_SUCCESS = 0,
    // SwappableNodeRefStack integrity checks
    NODEREFS_WIDTH_ZERO_NOT_UNITY = 100,
    NODEREFS_WIDTH_DECREASING,
    // Node integrity checks
    NODE_HEIGHT_ZERO = 200,
    NODE_HEIGHT_EXCEEDS_HEADNODE,
    NODE_NON_NULL_AFTER_NULL,
    NODE_SELF_REFERENCE,
    NODE_REFERENCES_NOT_IN_GLOBAL_SET,
    // HeadNode integrity checks
    HEADNODE_CONTAINS_NULL = 300,
    HEADNODE_COUNT_MISMATCH,
    HEADNODE_LEVEL_WIDTHS_MISMATCH,
    HEADNODE_DETECTS_CYCLIC_REFERENCE,
    HEADNODE_DETECTS_OUT_OF_ORDER,
};

#endif


// LICENSE_CHANGE_END


/// Forward reference
template<typename T, typename _Compare>
class Node;

/**
 * @brief A PoD struct that contains a pointer to a Node and a width that represents the coarser linked list span to the
 * next Node.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 */
template<typename T, typename _Compare=std::less<T> >
struct NodeRef {
    Node<T, _Compare> *pNode;
    size_t width;
};

/******************** SwappableNodeRefStack **********************/

/**
 * @brief Class that represents a stack of references to other nodes.
 *
 * Each reference is a NodeRef so a pointer to a Node and a width.
 * This just does simple bookkeeping on this stack.
 *
 * It also facilitates swapping references with another SwappableNodeRefStack when inserting or removing a Node.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 */
template<typename T, typename _Compare>
class SwappableNodeRefStack {
public:
    /**
     * Constructor. Initialises the swap level to 0.
     */
    SwappableNodeRefStack() : _swapLevel(0) {}

    // Const methods
    // -------------
    // Subscript read/write
    const NodeRef<T, _Compare> &operator[](size_t level) const;

    NodeRef<T, _Compare> &operator[](size_t level);

    /// Number of nodes referenced.
    size_t height() const {
        return _nodes.size();
    }

    /// The current swap level
    size_t swapLevel() const { return _swapLevel; }

    /// true if a swap can take place <tt>_swapLevel < height()</tt>
    bool canSwap() const { return _swapLevel < height(); }

    // Returns true if there is no record of p in my data that
    // could lead to circular references
    bool noNodePointerMatches(const Node<T, _Compare> *p) const;

    // Returns true if all pointers in my data are equal to p.
    bool allNodePointerMatch(const Node<T, _Compare> *p) const;

    // Non-const methods
    // -----------------
    /// Add a new reference
    void push_back(Node<T, _Compare> *p, size_t w) {
        struct NodeRef<T, _Compare> val = {p, w};
        _nodes.push_back(val);
    }

    /// Remove top reference
    void pop_back() {
        _nodes.pop_back();
    }

    // Swap reference at current swap level with another SwappableNodeRefStack
    void swap(SwappableNodeRefStack<T, _Compare> &val);

    /// Reset the swap level (for example before starting a remove).
    void resetSwapLevel() { _swapLevel = 0; }

    /// Increment the swap level.
    /// This is used when removing nodes where the parent node can record to what level it has made its adjustments
    /// so the grand parent knows where to start.
    ///
    /// For this reason the _swapLevel can easily be <tt>>= _nodes.size()</tt>.
    void incSwapLevel() { ++_swapLevel; }

    IntegrityCheck lacksIntegrity() const;

    // Returns an estimate of the memory usage of an instance
    size_t size_of() const;

	// Resets to the construction state
	void clear() { _swapLevel = 0; _nodes.clear(); }

protected:
    /// Stack of NodeRef node references.
    std::vector<struct NodeRef<T, _Compare> > _nodes;
    /// The current swap level.
    size_t _swapLevel;

private:
    /// Prevent cctor
    SwappableNodeRefStack(const SwappableNodeRefStack &that);

    /// Prevent operator=
    SwappableNodeRefStack &operator=(const SwappableNodeRefStack &that) const;
};

/**
 * The readable NodeRef at the given level.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param level The level.
 * @return A reference to the Node.
 */
template<typename T, typename _Compare>
const NodeRef<T, _Compare> &SwappableNodeRefStack<T, _Compare>::operator[](size_t level) const {
    // NOTE: No bounds checking on vector::operator[], so this assert will do
    assert(level < _nodes.size());
    return _nodes[level];
}

/**
 * The writeable NodeRef at the given level.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param level The level.
 * @return A reference to the Node.
 */
template<typename T, typename _Compare>
NodeRef<T, _Compare> &SwappableNodeRefStack<T, _Compare>::operator[](size_t level) {
    // NOTE: No bounds checking on vector::operator[], so this assert will do
    assert(level < _nodes.size());
    return _nodes[level];
}

/**
 * Whether all node references are swapped.
 * Should be true after an insert operation.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param p The Node.
 * @return true if all the Node references are swapped (none are referring to the given Node).
 */
template<typename T, typename _Compare>
bool SwappableNodeRefStack<T, _Compare>::noNodePointerMatches(const Node<T, _Compare> *p) const {
    for (size_t level = height(); level-- > 0;) {
        if (p == _nodes[level].pNode) {
            return false;
        }
    }
    return true;
}

/**
 * Returns true if all pointers in my data are equal to p.
 * Should be true after a remove operation.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param p The Node.
 * @return true if all the Node references are un-swapped (all are referring to the given Node).
 */
template<typename T, typename _Compare>
bool SwappableNodeRefStack<T, _Compare>::allNodePointerMatch(const Node<T, _Compare> *p) const {
    for (size_t level = height(); level-- > 0;) {
        if (p != _nodes[level].pNode) {
            return false;
        }
    }
    return true;
}

/**
 * Swap references with another SwappableNodeRefStack at the current swap level.
 * This also increments the swap level.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param val The SwappableNodeRefStack.
 */
template<typename T, typename _Compare>
void SwappableNodeRefStack<T, _Compare>::swap(SwappableNodeRefStack<T, _Compare> &val) {
    assert(_swapLevel < height());
    NodeRef<T, _Compare> temp = val[_swapLevel];
    val[_swapLevel] = _nodes[_swapLevel];
    _nodes[_swapLevel] = temp;
    ++_swapLevel;
}

/**
 * This checks the internal consistency of the object. It returns
 * INTEGRITY_SUCCESS [0] if successful or non-zero on error.
 * The tests are:
 *
 * - Widths must all be >= 1
 * - Widths must be weakly increasing with increasing level
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @return An IntegrityCheck enum.
 */
template<typename T, typename _Compare>
IntegrityCheck SwappableNodeRefStack<T, _Compare>::lacksIntegrity() const {
    if (height()) {
        if (_nodes[0].width != 1) {
            return NODEREFS_WIDTH_ZERO_NOT_UNITY;
        }
        for (size_t level = 1; level < height(); ++level) {
            if (_nodes[level].width < _nodes[level - 1].width) {
                return NODEREFS_WIDTH_DECREASING;
            }
        }
    }
    return INTEGRITY_SUCCESS;
}

/**
 * Returns an estimate of the memory usage of an instance
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @return The memory estimate.
 */
template<typename T, typename _Compare>
size_t SwappableNodeRefStack<T, _Compare>::size_of() const {
    return sizeof(*this) + _nodes.capacity() * sizeof(struct NodeRef<T>);
}

/********************* END: SwappableNodeRefStack ****************************/

#endif // SkipList_NodeRefs_h


// LICENSE_CHANGE_END



// LICENSE_CHANGE_BEGIN
// The following code up to LICENSE_CHANGE_END is subject to THIRD PARTY LICENSE #20
// See the end of this file for a list

/**
 * @file
 *
 * Project: skiplist
 *
 * Concurrency Tests.
 *
 * Created by Paul Ross on 03/12/2015.
 *
 * Copyright (c) 2015-2023 Paul Ross. All rights reserved.
 *
 * @code
 * MIT License
 *
 * Copyright (c) 2015-2023 Paul Ross
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * @endcode
 */

#ifndef SkipList_Node_h
#define SkipList_Node_h



#if __cplusplus < 201103L
#define nullptr NULL
#endif

/**************************** Node *********************************/

/**
 * @brief A single node in a Skip List containing a value and references to other downstream Node objects.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 */
template <typename T, typename _Compare>
class Node {
public:
	struct _Pool {
		explicit _Pool(_Compare _cmp) : _compare(_cmp), cache(nullptr) {
		}
		~_Pool() {
			delete cache;
		}
		Node *Allocate(const T &value) {
			if (cache) {
				Node *result = cache;
				cache = nullptr;
				result->Initialize(value);
				return result;
			}

			return new Node(value, _compare, *this);
		}

		T Release(Node *pNode) {
			T result = pNode->value();
			std::swap(pNode, cache);
			delete pNode;
			return result;
		}

		_Compare _compare;
		Node* cache;
		pcg32_fast prng;
	};

    Node(const T &value, _Compare _cmp, _Pool &pool);
    // Const methods
    //
    /// Returns the node value
    const T &value() const { return _value; }
    // Returns true if the value is present in the skip list from this node onwards.
    bool has(const T &value) const;
    // Returns the value at the index in the skip list from this node onwards.
    // Will return nullptr is not found.
    const Node<T, _Compare> *at(size_t idx) const;
    // Computes index of the first occurrence of a value
    bool index(const T& value, size_t &idx, size_t level) const;
    /// Number of linked lists that this node engages in, minimum 1.
    size_t height() const { return _nodeRefs.height(); }
    // Return the pointer to the next node at level 0
    const Node<T, _Compare> *next() const;
    // Return the width at given level.
    size_t width(size_t level) const;
    // Return the node pointer at given level, only used for HeadNode
    // integrity checks.
    const Node<T, _Compare> *pNode(size_t level) const;

    // Non-const methods
    /// Get a reference to the node references
    SwappableNodeRefStack<T, _Compare> &nodeRefs() { return _nodeRefs; }
    /// Get a reference to the node references
    const SwappableNodeRefStack<T, _Compare> &nodeRefs() const { return _nodeRefs; }
    // Insert a node
    Node<T, _Compare> *insert(const T &value);
    // Remove a node
    Node<T, _Compare> *remove(size_t call_level, const T &value);
    // An estimate of the number of bytes used by this node
    size_t size_of() const;

#ifdef INCLUDE_METHODS_THAT_USE_STREAMS
    void dotFile(std::ostream &os, size_t suffix = 0) const;
    void writeNode(std::ostream &os, size_t suffix = 0) const;
#endif // INCLUDE_METHODS_THAT_USE_STREAMS

    // Integrity checks, returns non-zero on failure
    IntegrityCheck lacksIntegrity(size_t headnode_height) const;
    IntegrityCheck lacksIntegrityRefsInSet(const std::set<const Node<T, _Compare>*> &nodeSet) const;

protected:
    Node<T, _Compare> *_adjRemoveRefs(size_t level, Node<T, _Compare> *pNode);

	void Initialize(const T &value) {
		_value = value;
		_nodeRefs.clear();
		do {
			_nodeRefs.push_back(this, _nodeRefs.height() ? 0 : 1);
		} while (_pool.prng() < _pool.prng.max() / 2);
	}

protected:
    T _value;
    SwappableNodeRefStack<T, _Compare> _nodeRefs;
    // Comparison function
    _Compare _compare;
    _Pool &_pool;
private:
    // Prevent cctor and operator=
    Node(const Node &that);
    Node &operator=(const Node &that) const;
};

/**
 * Constructor.
 * This also creates a SwappableNodeRefStack of random height by tossing a virtual coin.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param value The value of the Node.
 * @param _cmp The comparison function.
 */
template <typename T, typename _Compare>
Node<T, _Compare>::Node(const T &value, _Compare _cmp, _Pool &pool) : \
    _value(value), _compare(_cmp), _pool(pool) {
    Initialize(value);
}

/**
 * Returns true if the value is present in the skip list from this node onwards.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param value The value to look for.
 * @return true if the value is present in the skip list from this node onwards.
 */
template <typename T, typename _Compare>
bool Node<T, _Compare>::has(const T &value) const {
    assert(_nodeRefs.height());
    assert(value == value); // value can not be NaN for example
    // Effectively: if (value > _value) {
    if (_compare(_value, value)) {
        for (size_t l = _nodeRefs.height(); l-- > 0;) {
            if (_nodeRefs[l].pNode && _nodeRefs[l].pNode->has(value)) {
                return true;
            }
        }
        return false;
    }
    // Effectively: return value == _value; // false if value smaller
    return !_compare(value, _value) && !_compare(_value, value);
}

/**
 * Return a pointer to the n'th node.
 * Start (or continue) from the highest level, drop down a level if not found.
 * Return nullptr if not found at level 0.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param idx The index from hereon. If zero return this.
 * @return Pointer to the Node or nullptr.
 */
template <typename T, typename _Compare>
const Node<T, _Compare> *Node<T, _Compare>::at(size_t idx) const {
    assert(_nodeRefs.height());
    if (idx == 0) {
        return this;
    }
    for (size_t l = _nodeRefs.height(); l-- > 0;) {
        if (_nodeRefs[l].pNode && _nodeRefs[l].width <= idx) {
            return _nodeRefs[l].pNode->at(idx - _nodeRefs[l].width);
        }
    }
    return nullptr;
}

/**
 * Computes index of the first occurrence of a value.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param value The value to find.
 * @param idx The current index, this will be updated.
 * @param level The current level to search from.
 * @return true if found, false otherwise.
 */
template <typename T, typename _Compare>
bool Node<T, _Compare>::index(const T& value, size_t &idx, size_t level) const {
    assert(_nodeRefs.height());
    assert(value == value); // value can not be NaN for example
    assert(level < _nodeRefs.height());
    // Search has overshot, try again at a lower level.
    //if (_value > value) {
    if (_compare(value, _value)) {
        return false;
    }
    // First check if we match but we have been approached at a high level
    // as there may be an earlier node of the same value but with fewer
    // node references. In that case this search has to fail and try at a
    // lower level.
    // If however the level is 0 and we match then set the idx to 0 to mark us.
    // Effectively: if (_value == value) {
    if (!_compare(value, _value) && !_compare(_value, value)) {
        if (level > 0) {
            return false;
        }
        idx = 0;
        return true;
    }
    // Now work our way down
    // NOTE: We initialise l as level + 1 because l-- > 0 will decrement it to
    // the correct initial value
    for (size_t l = level + 1; l-- > 0;) {
        assert(l < _nodeRefs.height());
        if (_nodeRefs[l].pNode && _nodeRefs[l].pNode->index(value, idx, l)) {
            idx += _nodeRefs[l].width;
            return true;
        }
    }
    return false;
}

/**
 * Return the pointer to the next node at level 0.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @return The next node at level 0.
 */
template <typename T, typename _Compare>
const Node<T, _Compare> *Node<T, _Compare>::next() const {
    assert(_nodeRefs.height());
    return _nodeRefs[0].pNode;
}

/**
 * Return the width at given level.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param level The requested level.
 * @return The width.
 */
template <typename T, typename _Compare>
size_t Node<T, _Compare>::width(size_t level) const {
    assert(level < _nodeRefs.height());
    return _nodeRefs[level].width;
}

/**
 * Return the node pointer at given level, only used for HeadNode integrity checks.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param level The requested level.
 * @return The Node.
 */
template <typename T, typename _Compare>
const Node<T, _Compare> *Node<T, _Compare>::pNode(size_t level) const {
    assert(level < _nodeRefs.height());
    return _nodeRefs[level].pNode;
}

/**
 * Insert a new node with a value.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param value The value of the Node to insert.
 * @return Pointer to the new Node or nullptr on failure.
 */
template <typename T, typename _Compare>
Node<T, _Compare> *Node<T, _Compare>::insert(const T &value) {
    assert(_nodeRefs.height());
    assert(_nodeRefs.noNodePointerMatches(this));
    assert(! _nodeRefs.canSwap());
    assert(value == value); // NaN check for double

    // Effectively: if (value < _value) {
    if (_compare(value, _value)) {
        return nullptr;
    }
    // Recursive search for where to put the node
    Node<T, _Compare> *pNode = nullptr;
    size_t level = _nodeRefs.height();
    // Effectively: if (value >= _value) {
    if (! _compare(value, _value)) {
        for (level = _nodeRefs.height(); level-- > 0;) {
            if (_nodeRefs[level].pNode) {
                pNode = _nodeRefs[level].pNode->insert(value);
                if (pNode) {
                    break;
                }
            }
        }
    }
    // Effectively: if (! pNode && value >= _value) {
    if (! pNode && !_compare(value, _value)) {
        // Insert new node here
        pNode = _pool.Allocate(value);
        level = 0;
    }
    assert(pNode); // Should never get here unless a NaN has slipped through
    // Adjust references by marching up and recursing back.
    SwappableNodeRefStack<T, _Compare> &thatRefs = pNode->_nodeRefs;
    if (! thatRefs.canSwap()) {
        // Have an existing node or new node that is all swapped.
        // All I need to do is adjust my overshooting nodes and return
        // this for the caller to do the same.
        level = thatRefs.height();
        while (level < _nodeRefs.height()) {
            _nodeRefs[level].width += 1;
            ++level;
        }
        // The caller just has to increment its references that overshoot this
        assert(! _nodeRefs.canSwap());
        return this;
    }
    // March upwards
    if (level < thatRefs.swapLevel()) {
        assert(level == thatRefs.swapLevel() - 1);
        // This will happen when say a 3 high node, A, finds a 2 high
        // node, B, that creates a new 2+ high node. A will be at
        // level 1 and the new node will have swapLevel == 2 after
        // B has swapped.
        // Add the level to the accumulator at the next level
        thatRefs[thatRefs.swapLevel()].width += _nodeRefs[level].width;
        ++level;
    }
    size_t min_height = std::min(_nodeRefs.height(), thatRefs.height());
    while (level < min_height) {
        assert(thatRefs.canSwap());
        assert(level == thatRefs.swapLevel());
        assert(level < thatRefs.height());
        assert(_nodeRefs[level].width > 0);
        assert(thatRefs[level].width > 0);
        _nodeRefs[level].width -= thatRefs[level].width - 1;
        assert(_nodeRefs[level].width > 0);
        thatRefs.swap(_nodeRefs);
        if (thatRefs.canSwap()) {
            assert(thatRefs[thatRefs.swapLevel()].width == 0);
            thatRefs[thatRefs.swapLevel()].width = _nodeRefs[level].width;
        }
        ++level;
    }
    // Upwards march complete, now recurse back ('left').
    if (! thatRefs.canSwap()) {
        // All done with pNode locally.
        assert(level == thatRefs.height());
        assert(thatRefs.height() <= _nodeRefs.height());
        assert(level == thatRefs.swapLevel());
        // Adjust my overshooting nodes
        while (level < _nodeRefs.height()) {
            _nodeRefs[level].width += 1;
            ++level;
        }
        // The caller just has to increment its references that overshoot this
        assert(! _nodeRefs.canSwap());
        pNode = this;
    }
    return pNode;
}

/**
 * Adjust the Node references.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param level The level of the caller's node.
 * @param pNode The Node to swap references with.
 * @return The Node with swapped references.
 */
template <typename T, typename _Compare>
Node<T, _Compare> *Node<T, _Compare>::_adjRemoveRefs(size_t level, Node<T, _Compare> *pNode) {
    assert(pNode);
    SwappableNodeRefStack<T, _Compare> &thatRefs = pNode->_nodeRefs;

    assert(pNode != this);
    if (level < thatRefs.swapLevel()) {
        assert(level == thatRefs.swapLevel() - 1);
        ++level;
    }
    if (thatRefs.canSwap()) {
        assert(level == thatRefs.swapLevel());
        while (level < _nodeRefs.height() && thatRefs.canSwap()) {
            assert(level == thatRefs.swapLevel());
            // Compute the new width for the new node
            thatRefs[level].width += _nodeRefs[level].width - 1;
            thatRefs.swap(_nodeRefs);
            ++level;
        }
        assert(thatRefs.canSwap() || thatRefs.allNodePointerMatch(pNode));
    }
    // Decrement my widths as my refs are over the top of the missing pNode.
    while (level < _nodeRefs.height()) {
        _nodeRefs[level].width -= 1;
        ++level;
        thatRefs.incSwapLevel();
    }
    assert(! _nodeRefs.canSwap());
    return pNode;
}

/**
 * Remove a Node with the given value to be removed.
 * The return value must be deleted, the other Nodes have been adjusted as required.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param call_level Level the caller Node is at.
 * @param value Value of the detached Node to remove.
 * @return A pointer to the Node to be free'd or nullptr on failure.
 */
template <typename T, typename _Compare>
Node<T, _Compare> *Node<T, _Compare>::remove(size_t call_level,
                         const T &value) {
    assert(_nodeRefs.height());
    assert(_nodeRefs.noNodePointerMatches(this));

    Node<T, _Compare> *pNode = nullptr;
    // Effectively: if (value >= _value) {
    if (!_compare(value, _value)) {
        for (size_t level = call_level + 1; level-- > 0;) {
            if (_nodeRefs[level].pNode) {
                // Make progress to the right
                pNode = _nodeRefs[level].pNode->remove(level, value);
                if (pNode) {
                    return _adjRemoveRefs(level, pNode);
                }
            }
            // Make progress down
        }
    }
    if (! pNode) { // Base case
        // We only admit to being the node to remove if the caller is
        // approaching us from level 0. It is entirely likely that
        // the same (or an other) caller can see us at a higher level
        // but the recursion stack will not have been set up in the correct
        // step wise fashion so that the lower level references will
        // not be swapped.
        // Effectively: if (call_level == 0 && value == _value) {
        if (call_level == 0 && !_compare(value, _value) && !_compare(_value, value)) {
            _nodeRefs.resetSwapLevel();
            return this;
        }
    }
    assert(pNode == nullptr);
    return nullptr;
}

/*
 * This checks the internal consistency of a Node. It returns 0
 * if successful, non-zero on error. The tests are:
 *
 * - Height must be >= 1
 * - Height must not exceed HeadNode height.
 * - NULL pointer must not have a non-NULL above them.
 * - Node pointers must not be self-referential.
 */
/**
 * This checks the internal consistency of a Node. It returns 0
 * if successful, non-zero on error. The tests are:
 *
 * - Height must be >= 1
 * - Height must not exceed HeadNode height.
 * - NULL pointer must not have a non-NULL above them.
 * - Node pointers must not be self-referential.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param headnode_height Height of HeadNode.
 * @return An IntegrityCheck enum.
 */
template <typename T, typename _Compare>
IntegrityCheck Node<T, _Compare>::lacksIntegrity(size_t headnode_height) const {
    IntegrityCheck result = _nodeRefs.lacksIntegrity();
    if (result) {
        return result;
    }
    if (_nodeRefs.height() == 0) {
        return NODE_HEIGHT_ZERO;
    }
    if (_nodeRefs.height() > headnode_height) {
        return NODE_HEIGHT_EXCEEDS_HEADNODE;
    }
    // Test: All nodes above a nullprt must be nullptr
    size_t level = 0;
    while (level < _nodeRefs.height()) {
        if (! _nodeRefs[level].pNode) {
            break;
        }
        ++level;
    }
    while (level < _nodeRefs.height()) {
        if (_nodeRefs[level].pNode) {
            return NODE_NON_NULL_AFTER_NULL;
        }
        ++level;
    }
    // No reference should be to self.
    if (! _nodeRefs.noNodePointerMatches(this)) {
        return NODE_SELF_REFERENCE;
    }
    return INTEGRITY_SUCCESS;
}

/**
 * Checks that this Node is in the set held by the HeadNode.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param nodeSet Set of Nodes held by the HeadNode.
 * @return An IntegrityCheck enum.
 */
template <typename T, typename _Compare>
IntegrityCheck Node<T, _Compare>::lacksIntegrityRefsInSet(const std::set<const Node<T, _Compare>*> &nodeSet) const {
    size_t level = 0;
    while (level < _nodeRefs.height()) {
        if (nodeSet.count(_nodeRefs[level].pNode) == 0) {
            return NODE_REFERENCES_NOT_IN_GLOBAL_SET;
        }
        ++level;
    }
    return INTEGRITY_SUCCESS;
}

/**
 * Returns an estimate of the memory usage of an instance.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @return The memory estimate of this Node.
 */
template <typename T, typename _Compare>
size_t Node<T, _Compare>::size_of() const {
    // sizeof(*this) includes the size of _nodeRefs but _nodeRefs.size_of()
    // includes sizeof(_nodeRefs) so we need to subtract to avoid double counting
    return sizeof(*this) + _nodeRefs.size_of() - sizeof(_nodeRefs);
}


#ifdef INCLUDE_METHODS_THAT_USE_STREAMS

/**
 * Writes out this Node address.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param os Where to write.
 * @param suffix The suffix (node number).
 */
template <typename T, typename _Compare>
void Node<T, _Compare>::writeNode(std::ostream &os, size_t suffix) const {
    os << "\"node";
    os << suffix;
    os << std::hex << this << std::dec << "\"";
}

/**
 * Writes out a fragment of a DOT file representing this Node.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 * @param os Wheere to write.
 * @param suffix The node number.
 */
template <typename T, typename _Compare>
void Node<T, _Compare>::dotFile(std::ostream &os, size_t suffix) const {
    assert(_nodeRefs.height());
    writeNode(os, suffix);
    os << " [" << std::endl;
    os << "label = \"";
    for (size_t level = _nodeRefs.height(); level-- > 0;) {
        os << " { <w" << level + 1 << "> " << _nodeRefs[level].width;
        os << " | <f" << level + 1 << "> ";
        os << std::hex << _nodeRefs[level].pNode << std::dec;
        os << " }";
        os << " |";
    }
    os << " <f0> " << _value << "\"" << std::endl;
    os << "shape = \"record\"" << std::endl;
    os << "];" << std::endl;
    // Now edges
    for (size_t level = 0; level < _nodeRefs.height(); ++level) {
        writeNode(os, suffix);
        os << ":f" << level + 1 << " -> ";
        _nodeRefs[level].pNode->writeNode(os, suffix);
        //        writeNode(os, suffix);
        //        os << ":f" << i + 1 << " [];" << std::endl;
        os << ":w" << level + 1 << " [];" << std::endl;
    }
    assert(_nodeRefs.height());
    if (_nodeRefs[0].pNode) {
        _nodeRefs[0].pNode->dotFile(os, suffix);
    }
}

#endif // INCLUDE_METHODS_THAT_USE_STREAMS

/************************** END: Node *******************************/

#endif // SkipList_Node_h


// LICENSE_CHANGE_END



// LICENSE_CHANGE_BEGIN
// The following code up to LICENSE_CHANGE_END is subject to THIRD PARTY LICENSE #20
// See the end of this file for a list

/**
 * @file
 *
 * Project: skiplist
 *
 * Created by Paul Ross on 03/12/2015.
 *
 * Copyright (c) 2015-2023 Paul Ross. All rights reserved.
 *
 * @code
 * MIT License
 *
 * Copyright (c) 2017-2023 Paul Ross
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * @endcode
 */

#ifndef SkipList_HeadNode_h
#define SkipList_HeadNode_h

#include <functional>
//#ifdef SKIPLIST_THREAD_SUPPORT
//    #include <mutex>
//#endif
#include <vector>

#ifdef INCLUDE_METHODS_THAT_USE_STREAMS
#include <sstream>
#endif // INCLUDE_METHODS_THAT_USE_STREAMS



/** HeadNode
 *
 * @brief A HeadNode is a skip list. This is the single node leading to all other content Nodes.
 *
 * Example:
 *
 * @code
 *      OrderedStructs::SkipList::HeadNode<double> sl;
 *      for (int i = 0; i < 100; ++i) {
 *          sl.insert(i * 22.0 / 7.0);
 *      }
 *      sl.size(); // 100
 *      sl.at(50); // Value of 50 pi
 *      sl.remove(sl.at(50)); // Remove 50 pi
 * @endcode
 *
 * Created by Paul Ross on 03/12/2015.
 *
 * Copyright (c) 2015-2023 Paul Ross. All rights reserved.
 *
 * @tparam T The type of the Skip List Node values.
 * @tparam _Compare A comparison function for type T.
 */
template <typename T, typename _Compare=std::less<T>>
class HeadNode {
public:
    /**
     * Constructor for and Empty Skip List.
     *
     * @param cmp The comparison function for comparing Node values.
     */
    HeadNode(_Compare cmp=_Compare()) : _count(0), _compare(cmp), _pool(cmp) {
#ifdef INCLUDE_METHODS_THAT_USE_STREAMS
        _dot_file_subgraph = 0;
#endif
    }
    // Const methods
    //
    // Returns true if the value is present in the skip list.
    bool has(const T &value) const;
    // Returns the value at the index in the skip list.
    // Will throw an OrderedStructs::SkipList::IndexError if index out of range.
    const T &at(size_t index) const;
    // Find the value at index and write count values to dest.
    // Will throw a SkipList::IndexError if any index out of range.
    // This is useful for rolling median on even length lists where
    // the caller might want to implement the mean of two values.
    void at(size_t index, size_t count, std::vector<T> &dest) const;
    // Computes index of the first occurrence of a value
    // Will throw a ValueError if the value does not exist in the skip list
    size_t index(const T& value) const;
    // Number of values in the skip list.
    size_t size() const;
    // Non-const methods
    //
    // Insert a value.
    void insert(const T &value);
    // Remove a value and return it.
    // Will throw a ValueError is value not present.
    T remove(const T &value);

    // Const methods that are mostly used for debugging and visualisation.
    //
    // Number of linked lists that are in the skip list.
    size_t height() const;
    // Number of linked lists that the node at index has.
    // Will throw a SkipList::IndexError if idx out of range.
    size_t height(size_t idx) const;
    // The skip width of the node at index has.
    // May throw a SkipList::IndexError
    size_t width(size_t idx, size_t level) const;

#ifdef INCLUDE_METHODS_THAT_USE_STREAMS
    void dotFile(std::ostream &os) const;
    void dotFileFinalise(std::ostream &os) const;
#endif // INCLUDE_METHODS_THAT_USE_STREAMS

    // Returns non-zero if the integrity of this data structure is compromised
    // This is a thorough but expensive check!
    IntegrityCheck lacksIntegrity() const;
    // Estimate of the number of bytes used by the skip list
    size_t size_of() const;
    virtual ~HeadNode();

protected:
    void _adjRemoveRefs(size_t level, Node<T, _Compare> *pNode);
    const Node<T, _Compare> *_nodeAt(size_t idx) const;

protected:
    // Standardised way of throwing a ValueError
    void _throwValueErrorNotFound(const T &value) const;
    void _throwIfValueDoesNotCompare(const T &value) const;
    // Internal integrity checks
    IntegrityCheck _lacksIntegrityCyclicReferences() const;
    IntegrityCheck _lacksIntegrityWidthAccumulation() const;
    IntegrityCheck _lacksIntegrityNodeReferencesNotInList() const;
    IntegrityCheck _lacksIntegrityOrder() const;
protected:
    /// Number of nodes in the list.
    size_t _count;
    /// My node references, the size of this is the largest height in the list
    SwappableNodeRefStack<T, _Compare> _nodeRefs;
    /// Comparison function.
    _Compare _compare;
    typename Node<T, _Compare>::_Pool _pool;
#ifdef INCLUDE_METHODS_THAT_USE_STREAMS
    /// Used to count how many sub-graphs have been plotted
    mutable size_t _dot_file_subgraph;
#endif

private:
    /// Prevent cctor and operator=
    HeadNode(const HeadNode &that);
    HeadNode &operator=(const HeadNode &that) const;
};

/**
 * Returns true if the value is present in the skip list.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param value Value to check if it is in the Skip List.
 * @return true if in the Skip List.
 */
template <typename T, typename _Compare>
bool HeadNode<T, _Compare>::has(const T &value) const {
    _throwIfValueDoesNotCompare(value);
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    for (size_t l = _nodeRefs.height(); l-- > 0;) {
        assert(_nodeRefs[l].pNode);
        if (_nodeRefs[l].pNode->has(value)) {
            return true;
        }
    }
    return false;
}

/**
 * Returns the value at a particular index.
 * Will throw an OrderedStructs::SkipList::IndexError if index out of range.
 *
 * If @ref SKIPLIST_THREAD_SUPPORT is defined this will block.
 *
 * See _throw_exceeds_size() that does the throw.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param index The index.
 * @return The value at that index.
 */
template <typename T, typename _Compare>
const T &HeadNode<T, _Compare>::at(size_t index) const {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    const Node<T, _Compare> *pNode = _nodeAt(index);
    assert(pNode);
    return pNode->value();
}

/**
 * Find the count number of value starting at index and write them to dest.
 *
 * Will throw a OrderedStructs::SkipList::IndexError if any index out of range.
 *
 * This is useful for rolling median on even length lists where the caller might want to implement the mean of two
 * values.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param index The index.
 * @param count The number of values to retrieve.
 * @param dest The vector of values
 */
template <typename T, typename _Compare>
void HeadNode<T, _Compare>::at(size_t index, size_t count,
                               std::vector<T> &dest) const {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    dest.clear();
    const Node<T, _Compare> *pNode = _nodeAt(index);
    // _nodeAt will (should) throw an IndexError so this
    // assert should always be true
    assert(pNode);
    while (count) {
        if (! pNode) {
            _throw_exceeds_size(_count);
        }
        dest.push_back(pNode->value());
        pNode = pNode->next();
        --count;
    }
}

/**
 * Computes index of the first occurrence of a value
 * Will throw a OrderedStructs::SkipList::ValueError if the value does not exist in the skip list
 * Will throw a OrderedStructs::SkipList::FailedComparison if the value is not comparable.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param value The value to search for.
 * @return
 */
template <typename T, typename _Compare>
size_t HeadNode<T, _Compare>::index(const T& value) const {
    _throwIfValueDoesNotCompare(value);
    size_t idx;

#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    for (size_t l = _nodeRefs.height(); l-- > 0;) {
        assert(_nodeRefs[l].pNode);
        if (_nodeRefs[l].pNode->index(value, idx, l)) {
            idx += _nodeRefs[l].width;
            assert(idx > 0);
            return idx - 1;
        }
    }
    _throwValueErrorNotFound(value);
    return 0;
}

/**
 * Return the number of values in the Skip List.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @return The number of values in the Skip List.
 */
template <typename T, typename _Compare>
size_t HeadNode<T, _Compare>::size() const {
    return _count;
}

template <typename T, typename _Compare>
size_t HeadNode<T, _Compare>::height() const {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    size_t val = _nodeRefs.height();
    return val;
}

/**
 * Return the number of linked lists that the node at index has.
 *
 * Will throw a OrderedStructs::SkipList::IndexError if the index out of range.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param idx The index of the Skip List node.
 * @return The number of linked lists that the node at the index has.
 */
template <typename T, typename _Compare>
size_t HeadNode<T, _Compare>::height(size_t idx) const {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    const Node<T, _Compare> *pNode = _nodeAt(idx);
    assert(pNode);
    return pNode->height();
}

/**
 * The skip width of the Node at index has at the given level.
 * Will throw an IndexError if the index is out of range.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param idx The index.
 * @param level The level.
 * @return Width of Node.
 */
template <typename T, typename _Compare>
size_t HeadNode<T, _Compare>::width(size_t idx, size_t level) const {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    // Will throw if out of range.
    const Node<T, _Compare> *pNode = _nodeAt(idx);
    assert(pNode);
    if (level >= pNode->height()) {
        _throw_exceeds_size(pNode->height());
    }
    return pNode->nodeRefs()[level].width;
}

/**
 * Find the Node at the given index.
 * Will throw an IndexError if the index is out of range.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param idx The index.
 * @return The Node.
 */
template <typename T, typename _Compare>
const Node<T, _Compare> *HeadNode<T, _Compare>::_nodeAt(size_t idx) const {
    if (idx < _count) {
        for (size_t l = _nodeRefs.height(); l-- > 0;) {
            if (_nodeRefs[l].pNode && _nodeRefs[l].width <= idx + 1) {
                size_t new_index = idx + 1 - _nodeRefs[l].width;
                const Node<T, _Compare> *pNode = _nodeRefs[l].pNode->at(new_index);
                if (pNode) {
                    return pNode;
                }
            }
        }
    }
    assert(idx >= _count);
    _throw_exceeds_size(_count);
    // Should not get here as _throw_exceeds_size() will always throw.
    return NULL;
}

/**
 * Insert a value.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param value
 */
template <typename T, typename _Compare>
void HeadNode<T, _Compare>::insert(const T &value) {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#ifdef SKIPLIST_THREAD_SUPPORT_TRACE
    std::cout << "HeadNode insert() thread: " << std::this_thread::get_id() << std::endl;
#endif
#endif
    Node<T, _Compare> *pNode = nullptr;
    size_t level = _nodeRefs.height();

    _throwIfValueDoesNotCompare(value);
    while (level-- > 0) {
        assert(_nodeRefs[level].pNode);
        pNode = _nodeRefs[level].pNode->insert(value);
        if (pNode) {
            break;
        }
    }
    if (! pNode) {
        pNode = _pool.Allocate(value);
        level = 0;
    }
    assert(pNode);
    SwappableNodeRefStack<T, _Compare> &thatRefs = pNode->nodeRefs();
    if (thatRefs.canSwap()) {
        // Expand this to that
        while (_nodeRefs.height() < thatRefs.height()) {
            _nodeRefs.push_back(nullptr, _count + 1);
        }
        if (level < thatRefs.swapLevel()) {
            // Happens when we were originally, say 3 high (max height of any
            // previously seen node). Then a node is created
            // say 5 high. In that case this will be at level 2 and
            // thatRefs.swapLevel() will be 3
            assert(level + 1 == thatRefs.swapLevel());
            thatRefs[thatRefs.swapLevel()].width += _nodeRefs[level].width;
            ++level;
        }
        // Now swap
        while (level < _nodeRefs.height() && thatRefs.canSwap()) {
            assert(thatRefs.canSwap());
            assert(level == thatRefs.swapLevel());
            _nodeRefs[level].width -= thatRefs[level].width - 1;
            thatRefs.swap(_nodeRefs);
            if (thatRefs.canSwap()) {
                assert(thatRefs[thatRefs.swapLevel()].width == 0);
                thatRefs[thatRefs.swapLevel()].width = _nodeRefs[level].width;
            }
            ++level;
        }
        // Check all references swapped
        assert(! thatRefs.canSwap());
        // Check that all 'this' pointers created on construction have been moved
        assert(thatRefs.noNodePointerMatches(pNode));
    }
    if (level < thatRefs.swapLevel()) {
        // Happens when we are, say 5 high then a node is created
        // and consumed by the next node say 3 high. In that case this will be
        // at level 2 and thatRefs.swapLevel() will be 3
        assert(level + 1 == thatRefs.swapLevel());
        ++level;
    }
    // Increment my widths as my references are now going over the top of
    // pNode.
    while (level < _nodeRefs.height() && level >= thatRefs.height()) {
        _nodeRefs[level++].width += 1;
    }
    ++_count;
#ifdef SKIPLIST_THREAD_SUPPORT
#ifdef SKIPLIST_THREAD_SUPPORT_TRACE
    std::cout << "HeadNode insert() thread: " << std::this_thread::get_id() << " DONE" << std::endl;
#endif
#endif
}

/**
 * Adjust references >= level for removal of the node pNode.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param level Current level.
 * @param pNode Node to swap references with.
 */
template <typename T, typename _Compare>
void HeadNode<T, _Compare>::_adjRemoveRefs(size_t level,
                                           Node<T, _Compare> *pNode) {
    assert(pNode);
    SwappableNodeRefStack<T, _Compare> &thatRefs = pNode->nodeRefs();

    // Swap all remaining levels
    // This assertion checks that if swapping can take place we must be at the
    // same level.
    assert(! thatRefs.canSwap() || level == thatRefs.swapLevel());
    while (level < _nodeRefs.height() && thatRefs.canSwap()) {
        assert(level == thatRefs.swapLevel());
        // Compute the new width for the new node
        thatRefs[level].width += _nodeRefs[level].width - 1;
        thatRefs.swap(_nodeRefs);
        ++level;
        if (! thatRefs.canSwap()) {
            break;
        }
    }
    assert(! thatRefs.canSwap());
    // Decrement my widths as my references are now going over the top of
    // pNode.
    while (level < _nodeRefs.height()) {
        _nodeRefs[level++].width -= 1;
    }
    // Decrement my stack while top has a NULL pointer.
    while (_nodeRefs.height() && ! _nodeRefs[_nodeRefs.height() - 1].pNode) {
        _nodeRefs.pop_back();
    }
}

/**
 * Remove a Node with a value.
 * May throw a ValueError if the value is not found.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param value The value in the Node to remove.
 * @return The value removed.
 */
template <typename T, typename _Compare>
T HeadNode<T, _Compare>::remove(const T &value) {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#ifdef SKIPLIST_THREAD_SUPPORT_TRACE
    std::cout << "HeadNode remove() thread: " << std::this_thread::get_id() << std::endl;
#endif
#endif
    Node<T, _Compare> *pNode = nullptr;
    size_t level;

    _throwIfValueDoesNotCompare(value);
    for (level = _nodeRefs.height(); level-- > 0;) {
        assert(_nodeRefs[level].pNode);
        pNode = _nodeRefs[level].pNode->remove(level, value);
        if (pNode) {
            break;
        }
    }
    if (! pNode) {
        _throwValueErrorNotFound(value);
    }
    // Take swap level as some swaps will have been dealt with by the remove() above.
    _adjRemoveRefs(pNode->nodeRefs().swapLevel(), pNode);
    --_count;
    T ret_val = _pool.Release(pNode);
#ifdef SKIPLIST_THREAD_SUPPORT_TRACE
    std::cout << "HeadNode remove() thread: " << std::this_thread::get_id() << " DONE" << std::endl;
#endif
    return ret_val;
}

/**
 * Throw a ValueError in a consistent fashion.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param value The value to put into the ValueError.
 */
template <typename T, typename _Compare>
void HeadNode<T, _Compare>::_throwValueErrorNotFound(const T &value) const {
#ifdef INCLUDE_METHODS_THAT_USE_STREAMS
    std::ostringstream oss;
    oss << "Value " << value << " not found.";
    std::string err_msg = oss.str();
#else
    std::string err_msg = "Value not found.";
#endif
    throw ValueError(err_msg);
}

/**
 * Checks that the value == value.
 * This will throw a FailedComparison if that is not the case, for example NaN.
 *
 * @note
 * The Node class is (should be) not directly accessible by the user so we can just assert(value == value) in Node.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param value
 */
template <typename T, typename _Compare>
void HeadNode<T, _Compare>::_throwIfValueDoesNotCompare(const T &value) const {
    if (value != value) {
        throw FailedComparison(
            "Can not work with something that does not compare equal to itself.");
    }
}

/**
 * This tests that at every level >= 0 the sequence of node pointers
 * at that level does not contain a cyclic reference.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @return An IntegrityCheck enum.
 */
template <typename T, typename _Compare>
IntegrityCheck HeadNode<T, _Compare>::_lacksIntegrityCyclicReferences() const {
    assert(_nodeRefs.height());
    // Check for cyclic references at each level
    for (size_t level = 0; level < _nodeRefs.height(); ++level) {
        Node<T, _Compare> *p1 = _nodeRefs[level].pNode;
        Node<T, _Compare> *p2 = _nodeRefs[level].pNode;
        while (p1 && p2) {
            p1 = p1->nodeRefs()[level].pNode;
            if (p2->nodeRefs()[level].pNode) {
                p2 = p2->nodeRefs()[level].pNode->nodeRefs()[level].pNode;
            } else {
                p2 = nullptr;
            }
            if (p1 && p2 && p1 == p2) {
                return HEADNODE_DETECTS_CYCLIC_REFERENCE;
            }
        }
    }
    return INTEGRITY_SUCCESS;
}

/**
 * This tests that at every level > 0 the node to node width is the same
 * as the accumulated node to node widths at level - 1.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @return An IntegrityCheck enum.
 */
template <typename T, typename _Compare>
IntegrityCheck HeadNode<T, _Compare>::_lacksIntegrityWidthAccumulation() const {
    assert(_nodeRefs.height());
    for (size_t level = 1; level < _nodeRefs.height(); ++level) {
        const Node<T, _Compare> *pl = _nodeRefs[level].pNode;
        const Node<T, _Compare> *pl_1 = _nodeRefs[level - 1].pNode;
        assert(pl && pl_1); // No nulls allowed in HeadNode
        size_t wl = _nodeRefs[level].width;
        size_t wl_1 = _nodeRefs[level - 1].width;
        while (true) {
            while (pl != pl_1) {
                assert(pl_1); // Could only happen if a lower reference was NULL and the higher non-NULL.
                wl_1 += pl_1->width(level - 1);
                pl_1 = pl_1->pNode(level - 1);
            }
            if (wl != wl_1) {
                return HEADNODE_LEVEL_WIDTHS_MISMATCH;
            }
            if (pl == nullptr && pl_1 == nullptr) {
                break;
            }
            wl = pl->width(level);
            wl_1 = pl_1->width(level - 1);
            pl = pl->pNode(level);
            pl_1 = pl_1->pNode(level - 1);
        }
    }
    return INTEGRITY_SUCCESS;
}

/**
 * This tests the integrity of each Node.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @return An IntegrityCheck enum.
 */
template <typename T, typename _Compare>
IntegrityCheck HeadNode<T, _Compare>::_lacksIntegrityNodeReferencesNotInList() const {
    assert(_nodeRefs.height());

    IntegrityCheck result;
    std::set<const Node<T, _Compare>*> nodeSet;
    const Node<T, _Compare> *pNode = _nodeRefs[0].pNode;
    assert(pNode);

    // First gather all nodes, slightly awkward code here is so that
    // NULL is always included.
    nodeSet.insert(pNode);
    do {
        pNode = pNode->next();
        nodeSet.insert(pNode);
    } while (pNode);
    assert(nodeSet.size() == _count + 1); // All nodes plus NULL
    // Then test each node does not have pointers that are not in nodeSet
    pNode = _nodeRefs[0].pNode;
    while (pNode) {
        result = pNode->lacksIntegrityRefsInSet(nodeSet);
        if (result) {
            return result;
        }
        pNode = pNode->next();
    }
    return INTEGRITY_SUCCESS;
}

/**
 * Integrity check. Traverse the lowest level and check that the ordering
 * is correct according to the compare function. The HeadNode checks that the
 * Node(s) have correctly applied the compare function.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @return An IntegrityCheck enum.
 */
template <typename T, typename _Compare>
IntegrityCheck HeadNode<T, _Compare>::_lacksIntegrityOrder() const {
    if (_nodeRefs.height()) {
        // Traverse the lowest level list iteratively deleting as we go
        // Doing this recursivley could be expensive as we are at level 0.
        const Node<T, _Compare> *node = _nodeRefs[0].pNode;
        const Node<T, _Compare> *next;
        while (node) {
            next = node->next();
            if (next && _compare(next->value(), node->value())) {
                return HEADNODE_DETECTS_OUT_OF_ORDER;
            }
            node = next;
        }
    }
    return INTEGRITY_SUCCESS;
}

/**
 * Full integrity check.
 * This calls the other integrity check functions.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @return An IntegrityCheck enum.
 */
template <typename T, typename _Compare>
IntegrityCheck HeadNode<T, _Compare>::lacksIntegrity() const {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    if (_nodeRefs.height()) {
        IntegrityCheck result = _nodeRefs.lacksIntegrity();
        if (result) {
            return result;
        }
        if (! _nodeRefs.noNodePointerMatches(nullptr)) {
            return HEADNODE_CONTAINS_NULL;
        }
        // Check all nodes for integrity
        const Node<T, _Compare> *pNode = _nodeRefs[0].pNode;
        while (pNode) {
            result = pNode->lacksIntegrity(_nodeRefs.height());
            if (result) {
                return result;
            }
            pNode = pNode->next();
        }
        // Check count against total number of nodes
        pNode = _nodeRefs[0].pNode;
        size_t total = 0;
        while (pNode) {
            total += pNode->nodeRefs()[0].width;
            pNode = pNode->next();
        }
        if (total != _count) {
            return HEADNODE_COUNT_MISMATCH;
        }
        result = _lacksIntegrityWidthAccumulation();
        if (result) {
            return result;
        }
        result = _lacksIntegrityCyclicReferences();
        if (result) {
            return result;
        }
        result = _lacksIntegrityNodeReferencesNotInList();
        if (result) {
            return result;
        }
        result = _lacksIntegrityOrder();
        if (result) {
            return result;
        }
    }
    return INTEGRITY_SUCCESS;
}

/**
 * Returns an estimate of the memory usage of an instance.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @return The size of the memory estimate.
 */
template <typename T, typename _Compare>
size_t HeadNode<T, _Compare>::size_of() const {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    // sizeof(*this) includes the size of _nodeRefs but _nodeRefs.size_of()
    // includes sizeof(_nodeRefs) so we need to subtract to avoid double counting
    size_t ret_val = sizeof(*this) + _nodeRefs.size_of() - sizeof(_nodeRefs);
    if (_nodeRefs.height()) {
        const Node<T, _Compare> *node = _nodeRefs[0].pNode;
        while (node) {
            ret_val += node->size_of();
            node = node->next();
        }
    }
    return ret_val;
}

/**
 * Destructor.
 * This deletes all Nodes.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 */
template <typename T, typename _Compare>
HeadNode<T, _Compare>::~HeadNode() {
    // Hmm could this deadlock?
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    if (_nodeRefs.height()) {
        // Traverse the lowest level list iteratively deleting as we go
        // Doing this recursivley could be expensive as we are at level 0.
        const Node<T, _Compare> *node = _nodeRefs[0].pNode;
        const Node<T, _Compare> *next;
        while (node) {
            next = node->next();
            delete node;
            --_count;
            node = next;
        }
    }
    assert(_count == 0);
}

#ifdef INCLUDE_METHODS_THAT_USE_STREAMS

/**
 * Create a DOT file of the internal representation.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param os Where to write the DOT file.
 */
template <typename T, typename _Compare>
void HeadNode<T, _Compare>::dotFile(std::ostream &os) const {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    if (_dot_file_subgraph == 0) {
        os << "digraph SkipList {" << std::endl;
        os << "label = \"SkipList.\"" << std::endl;
        os << "graph [rankdir = \"LR\"];" << std::endl;
        os << "node [fontsize = \"12\" shape = \"ellipse\"];" << std::endl;
        os << "edge [];" << std::endl;
        os << std::endl;
    }
    os << "subgraph cluster" << _dot_file_subgraph << " {" << std::endl;
    os << "style=dashed" << std::endl;
    os << "label=\"Skip list iteration " << _dot_file_subgraph << "\"" << std::endl;
    os << std::endl;
    os << "\"HeadNode" << _dot_file_subgraph;
    os << "\" [" << std::endl;
    os << "label = \"";
    // Write out the fields
    if (_nodeRefs.height()) {
        for (size_t level = _nodeRefs.height(); level-- > 0;) {
            os << "{ " << _nodeRefs[level].width << " | ";
            os << "<f" << level + 1 << "> ";
            os << std::hex << _nodeRefs[level].pNode << std::dec;
            os << "}";
            if (level > 0) {
                os << " | ";
            }
        }
    } else {
        os << "Empty HeadNode";
    }
    os << "\"" << std::endl;
    os << "shape = \"record\"" << std::endl;
    os << "];" << std::endl;
    // Edges for head node
    for (size_t level = 0; level < _nodeRefs.height(); ++level) {
        os << "\"HeadNode";
        os << _dot_file_subgraph;
        os << "\":f" << level + 1 << " -> ";
        _nodeRefs[level].pNode->writeNode(os, _dot_file_subgraph);
        os << ":w" << level + 1 << " [];" << std::endl;
    }
    os << std::endl;
    // Now all nodes via level 0, if non-empty
    if (_nodeRefs.height()) {
        Node<T, _Compare> *pNode = this->_nodeRefs[0].pNode;
        pNode->dotFile(os, _dot_file_subgraph);
    }
    os << std::endl;
    // NULL, the sentinal node
    if (_nodeRefs.height()) {
        os << "\"node";
        os << _dot_file_subgraph;
        os << "0x0\" [label = \"";
        for (size_t level = _nodeRefs.height(); level-- > 0;) {
            os << "<w" << level + 1 << "> NULL";
            if (level) {
                os << " | ";
            }
        }
        os << "\" shape = \"record\"];" << std::endl;
    }
    // End: "subgraph cluster1 {"
    os << "}" << std::endl;
    os << std::endl;
    _dot_file_subgraph += 1;
}

/**
 * Finalise the DOT file of the internal representation.
 *
 * @tparam T Type of the values in the Skip List.
 * @tparam _Compare Compare function.
 * @param os Where to write the DOT file.
 */
template <typename T, typename _Compare>
void HeadNode<T, _Compare>::dotFileFinalise(std::ostream &os) const {
#ifdef SKIPLIST_THREAD_SUPPORT
    std::lock_guard<std::mutex> lock(gSkipListMutex);
#endif
    if (_dot_file_subgraph > 0) {
        // Link the nodes together with an invisible node.
        //    node0 [shape=record, label = "<f0> | <f1> | <f2> | <f3> | <f4> | <f5> | <f6> | <f7> | <f8> | ",
        //           style=invis,
        //           width=0.01];
        os << "node0 [shape=record, label = \"";
        for (size_t i = 0; i < _dot_file_subgraph; ++i) {
            os << "<f" << i << "> | ";
        }
        os << "\", style=invis, width=0.01];" << std::endl;
        // Now:
        //    node0:f0 -> HeadNode [style=invis];
        //    node0:f1 -> HeadNode1 [style=invis];
        for (size_t i = 0; i < _dot_file_subgraph; ++i) {
            os << "node0:f" << i << " -> HeadNode" << i;
            os << " [style=invis];" << std::endl;
        }
        _dot_file_subgraph = 0;
    }
    os << "}" << std::endl;
}

#endif // INCLUDE_METHODS_THAT_USE_STREAMS

/************************** END: HeadNode *******************************/

#endif // SkipList_HeadNode_h


// LICENSE_CHANGE_END


    } // namespace skip_list
} // namespace duckdb_skiplistlib

#endif /* defined(__SkipList__SkipList__) */


// LICENSE_CHANGE_END


namespace duckdb_skiplistlib {
namespace skip_list {

// This throws an IndexError when the index value >= size.
// If possible the error will have an informative message.
#ifdef INCLUDE_METHODS_THAT_USE_STREAMS
void _throw_exceeds_size(size_t index) {
    std::ostringstream oss;
    oss << "Index out of range 0 <= index < " << index;
    std::string err_msg = oss.str();
#else
void _throw_exceeds_size(size_t /* index */) {
    std::string err_msg = "Index out of range.";
#endif
    throw IndexError(err_msg);
}

#ifdef SKIPLIST_THREAD_SUPPORT
    std::mutex gSkipListMutex;
#endif


} // namespace SkipList
} // namespace OrderedStructs


// LICENSE_CHANGE_END
