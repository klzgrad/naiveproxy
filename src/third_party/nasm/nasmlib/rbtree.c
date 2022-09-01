/* ----------------------------------------------------------------------- *
 *   
 *   Copyright 1996-2009 The NASM Authors - All Rights Reserved
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
 * Simple implementation of a left-leaning red-black tree with 64-bit
 * integer keys.  The search operation will return the highest node <=
 * the key; only search and insert are supported, but additional
 * standard llrbtree operations can be coded up at will.
 *
 * See http://www.cs.princeton.edu/~rs/talks/LLRB/RedBlack.pdf for
 * information about left-leaning red-black trees.
 */

#include "rbtree.h"

struct rbtree *rb_search(struct rbtree *tree, uint64_t key)
{
    struct rbtree *best = NULL;

    while (tree) {
	if (tree->key == key)
	    return tree;
	else if (tree->key > key)
	    tree = tree->left;
	else {
	    best = tree;
	    tree = tree->right;
	}
    }
    return best;
}

static bool is_red(struct rbtree *h)
{
    return h && h->red;
}

static struct rbtree *rotate_left(struct rbtree *h)
{
    struct rbtree *x = h->right;
    h->right = x->left;
    x->left = h;
    x->red = x->left->red;
    x->left->red = true;
    return x;
}

static struct rbtree *rotate_right(struct rbtree *h)
{
    struct rbtree *x = h->left;
    h->left = x->right;
    x->right = h;
    x->red = x->right->red;
    x->right->red = true;
    return x;
}

static void color_flip(struct rbtree *h)
{
    h->red = !h->red;
    h->left->red = !h->left->red;
    h->right->red = !h->right->red;
}

struct rbtree *rb_insert(struct rbtree *tree, struct rbtree *node)
{
    if (!tree) {
	node->red = true;
	return node;
    }

    if (is_red(tree->left) && is_red(tree->right))
	color_flip(tree);

    if (node->key < tree->key)
	tree->left = rb_insert(tree->left, node);
    else
	tree->right = rb_insert(tree->right, node);

    if (is_red(tree->right))
	tree = rotate_left(tree);

    if (is_red(tree->left) && is_red(tree->left->left))
	tree = rotate_right(tree);

    return tree;
}
