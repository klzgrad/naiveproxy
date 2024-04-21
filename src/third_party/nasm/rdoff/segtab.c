/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2014 The NASM Authors - All Rights Reserved
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

#include "rdfutils.h"
#include "segtab.h"

struct segtabnode {
    int localseg;
    int destseg;
    int32_t offset;

    struct segtabnode *left;
    struct segtabnode *right;
    /*
     * counts of how many are left or right, for use in reorganising
     * the tree
     */
    int leftcount;
    int rightcount;
};

/*
 * init_seglocations()
 * add_seglocation()
 * get_seglocation()
 * done_seglocation()
 *
 * functions used by write_output() to manipulate associations
 * between segment numbers and locations (which are built up on a per
 * module basis, but we only need one module at a time...)
 *
 * implementation: we build a binary tree.
 */

void init_seglocations(segtab * root)
{
    *root = NULL;
}

static void descend_tree_add(struct segtabnode **node,
                      int localseg, int destseg, int32_t offset)
{
    struct segtabnode *n;

    if (*node == NULL) {
        *node = nasm_malloc(sizeof(**node));
        if (!*node) {
            fprintf(stderr, "segment table: out of memory\n");
            exit(1);
        }
        (*node)->localseg = localseg;
        (*node)->offset = offset;
        (*node)->left = NULL;
        (*node)->leftcount = 0;
        (*node)->right = NULL;
        (*node)->rightcount = 0;
        (*node)->destseg = destseg;
        return;
    }

    if (localseg < (*node)->localseg) {
        (*node)->leftcount++;
        descend_tree_add(&(*node)->left, localseg, destseg, offset);

        if ((*node)->leftcount > (*node)->rightcount + 2) {
            n = *node;
            *node = n->left;
            n->left = (*node)->right;
            n->leftcount = (*node)->rightcount;
            (*node)->right = n;
            (*node)->rightcount = n->leftcount + n->rightcount + 1;
        }
    } else {
        (*node)->rightcount++;
        descend_tree_add(&(*node)->right, localseg, destseg, offset);

        if ((*node)->rightcount > (*node)->leftcount + 2) {
            n = *node;
            *node = n->right;
            n->right = (*node)->left;
            n->rightcount = (*node)->leftcount;
            (*node)->left = n;
            (*node)->leftcount = n->leftcount + n->rightcount + 1;
        }
    }
}

void add_seglocation(segtab * root, int localseg, int destseg, int32_t offset)
{
    descend_tree_add((struct segtabnode **)root, localseg, destseg,
                     offset);
}

int get_seglocation(segtab * root, int localseg, int *destseg,
                    int32_t *offset)
{
    struct segtabnode *n = (struct segtabnode *)*root;

    while (n && n->localseg != localseg) {
        if (localseg < n->localseg)
            n = n->left;
        else
            n = n->right;
    }
    if (n) {
        *destseg = n->destseg;
        *offset = n->offset;
        return 1;
    } else
        return 0;
}

static void freenode(struct segtabnode *n)
{
    if (!n)
        return;
    freenode(n->left);
    freenode(n->right);
    nasm_free(n);
}

void done_seglocations(segtab * root)
{
    freenode(*root);
    *root = NULL;
}

#if 0
void printnode(int i, struct segtabnode *n)
{
    if (!n)
        return;
    printnode(i + 1, n->left);
    printf("%*s%d %d %ld\n", i, "", n->localseg, n->destseg, n->offset);
    printnode(i + 1, n->right);
}

void printtable()
{
    printnode(0, root);
}
#endif
