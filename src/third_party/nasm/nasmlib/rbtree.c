/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2020 The NASM Authors - All Rights Reserved
 *   See the file AUTHORS included with the NASM distribution for
 *   the specific copyright holders.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------- */

/*
 * rbtree.c
 *
 * Simple implementation of a "left-leaning threaded red-black tree"
 * with 64-bit integer keys.  The search operation will return the
 * highest node <= the key; only search and insert are supported, but
 * additional standard llrbtree operations can be coded up at will.
 *
 * See http://www.cs.princeton.edu/~rs/talks/LLRB/RedBlack.pdf for
 * information about left-leaning red-black trees.
 *
 * The "threaded" part means that left and right pointers that would
 * otherwise be NULL are pointers to the in-order predecessor or
 * successor node. The only pointers that are NULL are the very left-
 * and rightmost, for which no corresponding side node exists.
 *
 * This, among other things, allows for efficient predecessor and
 * successor operations without requiring dedicated space for a parent
 * pointer.
 *
 * This implementation is robust for identical key values; such keys
 * will not have their insertion order preserved, and after insertion
 * of unrelated keys a lookup may return a different node for the
 * duplicated key, but the prev/next operations will always enumerate
 * all entries.
 *
 * The NULL pointers at the end are considered predecessor/successor
 * pointers, so if the corresponding flags are clear it is always safe
 * to access the pointed-to object without an explicit NULL pointer
 * check.
 */

#include "rbtree.h"
#include "nasmlib.h"

struct rbtree *rb_search(const struct rbtree *tree, uint64_t key)
{
    const struct rbtree *best = NULL;

    if (tree) {
        while (true) {
            if (tree->key > key) {
                if (tree->m.flags & RBTREE_NODE_PRED)
                    break;
                tree = tree->m.left;
            } else {
                best = tree;
                if (tree->key == key || (tree->m.flags & RBTREE_NODE_SUCC))
                    break;
                tree = tree->m.right;
            }
	}
    }
    return (struct rbtree *)best;
}

struct rbtree *rb_search_exact(const struct rbtree *tree, uint64_t key)
{
    struct rbtree *rv;

    rv = rb_search(tree, key);
    return (rv && rv->key == key) ? rv : NULL;
}

/* Reds two left in a row? */
static inline bool is_red_left_left(struct rbtree *h)
{
    return !(h->m.flags & RBTREE_NODE_PRED) &&
        !(h->m.left->m.flags & (RBTREE_NODE_BLACK|RBTREE_NODE_PRED)) &&
        !(h->m.left->m.left->m.flags & RBTREE_NODE_BLACK);
}

/* Node to the right is red? */
static inline bool is_red_right(struct rbtree *h)
{
    return !(h->m.flags & RBTREE_NODE_SUCC) &&
           !(h->m.right->m.flags & RBTREE_NODE_BLACK);
}

/* Both the left and right hand nodes are red? */
static inline bool is_red_both(struct rbtree *h)
{
    return !(h->m.flags & (RBTREE_NODE_PRED|RBTREE_NODE_SUCC))
        && !(h->m.left->m.flags & h->m.right->m.flags & RBTREE_NODE_BLACK);
}

static inline struct rbtree *rotate_left(struct rbtree *h)
{
    struct rbtree *x = h->m.right;
    enum rbtree_node_flags hf = h->m.flags;
    enum rbtree_node_flags xf = x->m.flags;

    if (xf & RBTREE_NODE_PRED) {
        h->m.right = x;
        h->m.flags = (hf & RBTREE_NODE_PRED)  | RBTREE_NODE_SUCC;
    } else {
        h->m.right = x->m.left;
        h->m.flags = hf & RBTREE_NODE_PRED;
    }
    x->m.flags = (hf & RBTREE_NODE_BLACK) | (xf & RBTREE_NODE_SUCC);
    x->m.left = h;

    return x;
}

static inline struct rbtree *rotate_right(struct rbtree *h)
{
    struct rbtree *x = h->m.left;
    enum rbtree_node_flags hf = h->m.flags;
    enum rbtree_node_flags xf = x->m.flags;

    if (xf & RBTREE_NODE_SUCC) {
        h->m.left = x;
        h->m.flags = (hf & RBTREE_NODE_SUCC)  | RBTREE_NODE_PRED;
    } else {
        h->m.left = x->m.right;
        h->m.flags = hf & RBTREE_NODE_SUCC;
    }
    x->m.flags = (hf & RBTREE_NODE_BLACK) | (xf & RBTREE_NODE_PRED);
    x->m.right = h;

    return x;
}

static inline void color_flip(struct rbtree *h)
{
    h->m.flags          ^= RBTREE_NODE_BLACK;
    h->m.left->m.flags  ^= RBTREE_NODE_BLACK;
    h->m.right->m.flags ^= RBTREE_NODE_BLACK;
}

static struct rbtree *
_rb_insert(struct rbtree *tree, struct rbtree *node);

struct rbtree *rb_insert(struct rbtree *tree, struct rbtree *node)
{
    /* Initialize node as if it was the sole member of the tree */

    nasm_zero(node->m);
    node->m.flags = RBTREE_NODE_PRED|RBTREE_NODE_SUCC;

    if (unlikely(!tree))
        return node;

    return _rb_insert(tree, node);
}

static struct rbtree *
_rb_insert(struct rbtree *tree, struct rbtree *node)
{
    /* Recursive part of the algorithm */

    /* Red on both sides? */
    if (is_red_both(tree))
	color_flip(tree);

    if (node->key < tree->key) {
        node->m.right  = tree;  /* Potential successor */
        if (tree->m.flags & RBTREE_NODE_PRED) {
            node->m.left   = tree->m.left;
            tree->m.flags &= ~RBTREE_NODE_PRED;
            tree->m.left   = node;
        } else {
            tree->m.left   = _rb_insert(tree->m.left, node);
        }
    } else {
        node->m.left   = tree;  /* Potential predecessor */
        if (tree->m.flags & RBTREE_NODE_SUCC) {
            node->m.right  = tree->m.right;
            tree->m.flags &= ~RBTREE_NODE_SUCC;
            tree->m.right  = node;
        } else {
            tree->m.right  = _rb_insert(tree->m.right, node);
        }
    }

    if (is_red_right(tree))
	tree = rotate_left(tree);

    if (is_red_left_left(tree))
	tree = rotate_right(tree);

    return tree;
}

struct rbtree *rb_first(const struct rbtree *tree)
{
    if (unlikely(!tree))
        return NULL;

    while (!(tree->m.flags & RBTREE_NODE_PRED))
        tree = tree->m.left;

    return (struct rbtree *)tree;
}

struct rbtree *rb_last(const struct rbtree *tree)
{
    if (unlikely(!tree))
        return NULL;

    while (!(tree->m.flags & RBTREE_NODE_SUCC))
        tree = tree->m.right;

    return (struct rbtree *)tree;
}

struct rbtree *rb_prev(const struct rbtree *node)
{
    struct rbtree *np = node->m.left;

    if (node->m.flags & RBTREE_NODE_PRED)
        return np;
    else
        return rb_last(np);
}

struct rbtree *rb_next(const struct rbtree *node)
{
    struct rbtree *np = node->m.right;

    if (node->m.flags & RBTREE_NODE_SUCC)
        return np;
    else
        return rb_first(np);
}
