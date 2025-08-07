/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* Part of this work is inspired by the original OpenBSD's tree.h */
/* $OpenBSD: tree.h,v 1.31 2023/03/08 04:43:09 guenther Exp $ */
/*
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "src/base/intrusive_tree.h"

namespace perfetto::base::internal {

namespace {

void RBSetBlackRed(RBNode* black, RBNode* red) {
  black->color = RBColor::BLACK;
  red->color = RBColor::RED;
}

void RBRotateLeft(RBNode** root, RBNode* elm, RBNode* tmp) {
  tmp = elm->right;
  if ((elm->right = tmp->left)) {
    tmp->left->parent = elm;
  }
  // RB_AUGMENT(elm);
  if ((tmp->parent = elm->parent)) {
    if (elm == elm->parent->left)
      elm->parent->left = tmp;
    else
      elm->parent->right = tmp;
  } else
    *root = tmp;
  tmp->left = elm;
  elm->parent = tmp;
  // RB_AUGMENT(tmp);
  // if ((tmp->parent))
  // RB_AUGMENT(tmp->parent);
}

void RBRotateRight(RBNode** root, RBNode* elm, RBNode* tmp) {
  tmp = elm->left;
  if ((elm->left = tmp->right)) {
    tmp->right->parent = elm;
  }
  // RB_AUGMENT(elm);
  if ((tmp->parent = elm->parent)) {
    if (elm == elm->parent->left)
      elm->parent->left = tmp;
    else
      elm->parent->right = tmp;
  } else
    *root = tmp;
  tmp->right = elm;
  elm->parent = tmp;
  // RB_AUGMENT(tmp);
  // if ((tmp->parent))
  // RB_AUGMENT(tmp->parent);
}

void RBRemoveColor(RBNode** root, RBNode* parent, RBNode* elm) {
  RBNode* tmp;
  while ((elm == nullptr || elm->color == RBColor::BLACK) && elm != *root) {
    if (parent->left == elm) {
      tmp = parent->right;
      if (tmp->color == RBColor::RED) {
        RBSetBlackRed(tmp, parent);
        RBRotateLeft(root, parent, tmp);
        tmp = parent->right;
      }
      if ((tmp->left == nullptr || tmp->left->color == RBColor::BLACK) &&
          (tmp->right == nullptr || tmp->right->color == RBColor::BLACK)) {
        tmp->color = RBColor::RED;
        elm = parent;
        parent = elm->parent;
      } else {
        if (tmp->right == nullptr || tmp->right->color == RBColor::BLACK) {
          RBNode* oleft;
          if ((oleft = tmp->left))
            oleft->color = RBColor::BLACK;
          tmp->color = RBColor::RED;
          RBRotateRight(root, tmp, oleft);
          tmp = parent->right;
        }
        tmp->color = parent->color;
        parent->color = RBColor::BLACK;
        if (tmp->right)
          tmp->right->color = RBColor::BLACK;
        RBRotateLeft(root, parent, tmp);
        elm = *root;
        break;
      }
    } else {
      tmp = parent->left;
      if (tmp->color == RBColor::RED) {
        RBSetBlackRed(tmp, parent);
        RBRotateRight(root, parent, tmp);
        tmp = parent->left;
      }
      if ((tmp->left == nullptr || tmp->left->color == RBColor::BLACK) &&
          (tmp->right == nullptr || tmp->right->color == RBColor::BLACK)) {
        tmp->color = RBColor::RED;
        elm = parent;
        parent = elm->parent;
      } else {
        if (tmp->left == nullptr || tmp->left->color == RBColor::BLACK) {
          RBNode* oright;
          if ((oright = tmp->right))
            oright->color = RBColor::BLACK;
          tmp->color = RBColor::RED;
          RBRotateLeft(root, tmp, oright);
          tmp = parent->left;
        }
        tmp->color = parent->color;
        parent->color = RBColor::BLACK;
        if (tmp->left)
          tmp->left->color = RBColor::BLACK;
        RBRotateRight(root, parent, tmp);
        elm = *root;
        break;
      }
    }
  }
  if (elm)
    elm->color = RBColor::BLACK;
}

}  // namespace

void RBInsertColor(RBNode** root, RBNode* elm) {
  RBNode *parent, *gparent, *tmp;
  while ((parent = elm->parent) && parent->color == RBColor::RED) {
    gparent = parent->parent;
    if (parent == gparent->left) {
      tmp = gparent->right;
      if (tmp && tmp->color == RBColor::RED) {
        tmp->color = RBColor::BLACK;
        RBSetBlackRed(parent, gparent);
        elm = gparent;
        continue;
      }
      if (parent->right == elm) {
        RBRotateLeft(root, parent, tmp);
        tmp = parent;
        parent = elm;
        elm = tmp;
      }
      RBSetBlackRed(parent, gparent);
      RBRotateRight(root, gparent, tmp);
    } else {
      tmp = gparent->left;
      if (tmp && tmp->color == RBColor::RED) {
        tmp->color = RBColor::BLACK;
        RBSetBlackRed(parent, gparent);
        elm = gparent;
        continue;
      }
      if (parent->left == elm) {
        RBRotateRight(root, parent, tmp);
        tmp = parent;
        parent = elm;
        elm = tmp;
      }
      RBSetBlackRed(parent, gparent);
      RBRotateLeft(root, gparent, tmp);
    }
  }
  (*root)->color = RBColor::BLACK;
}

void RBRemove(RBNode** root, RBNode* elm) {
  RBNode* child = elm;
  RBNode* parent = elm;
  RBNode* old = elm;
  RBColor color;

  if (elm->left == nullptr)
    child = elm->right;
  else if (elm->right == nullptr)
    child = elm->left;
  else {
    RBNode* left;
    elm = elm->right;
    while ((left = elm->left))
      elm = left;
    child = elm->right;
    parent = elm->parent;
    color = elm->color;
    if (child)
      child->parent = parent;
    if (parent) {
      if (parent->left == elm) {
        parent->left = child;
      } else {
        parent->right = child;
      }
      // RB_AUGMENT(parent);
    } else {
      *root = child;
    }
    if (elm->parent == old)
      parent = elm;
    *elm = *old;
    if (old->parent) {
      if (old->parent->left == old) {
        old->parent->left = elm;
      } else {
        old->parent->right = elm;
      }
      // RB_AUGMENT(old->parent);
    } else {
      *root = elm;
    }
    old->left->parent = elm;
    if (old->right)
      old->right->parent = elm;
    if (parent) {
      left = parent;
      // do {
      //   RB_AUGMENT(left);
      // } while ((left = left->parent));
    }
    goto color;
  }
  parent = elm->parent;
  color = elm->color;
  if (child)
    child->parent = parent;
  if (parent) {
    if (parent->left == elm)
      parent->left = child;
    else
      parent->right = child;
    // RB_AUGMENT(parent);
  } else {
    *root = child;
  }
color:
  if (color == RBColor::BLACK)
    RBRemoveColor(root, parent, child);
}

// Returns nullptr after reaching the last leaf (the max element).
const RBNode* RBNext(const RBNode* node) {
  if (node->right) {
    node = node->right;
    while (node->left)
      node = node->left;
  } else {
    if (node->parent && node == node->parent->left) {
      node = node->parent;
    } else {
      while (node->parent && node == node->parent->right) {
        node = node->parent;
      }
      node = node->parent;
    }
  }
  return node;
}

}  // namespace perfetto::base::internal
