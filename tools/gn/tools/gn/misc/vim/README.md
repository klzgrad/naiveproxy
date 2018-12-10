# GN vim syntax plugin

## Installation with a plugin manager

You can use modern plugin managers to download the GN repo and manage the vim
plugin:

Example config for [vim-plug](https://github.com/junegunn/vim-plug):

```
Plug 'https://gn.googlesource.com/gn', { 'rtp': 'tools/gn/misc/vim' }
```

Or, for [Vundle](https://github.com/VundleVim/Vundle.vim) users:

```
Plugin 'https://gn.googlesource.com/gn', { 'rtp': 'tools/gn/misc/vim' }
```

## Manual installation

If you don't use a plugin manager or would prefer to manage the GN repo
yourself, you can add this explicitly to `rtp` in your `.vimrc`:

```
set runtimepath+=/path/to/src/tools/gn/misc/vim
" ...
filetype plugin indent on " or a similar command to turn on filetypes in vim
```
