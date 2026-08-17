# Full copies of every file patch 09 modifies

These are not the patch. They are the sources the patch was generated *from*,
kept whole because the tree they lived in was a scratch directory under
`/private/tmp`, and `/private/tmp` was pruned out from under it: both git
repositories lost their objects, the pristine upstream checkout lost its `src/`
entirely, and the build tree lost the top-level scripts the Makefile shells out
to. What survived was `src/`, `include/`, and the built ELF.

That prune also broke patch generation silently. The staging step was

    (cd $BUILD && git diff -- src include Makefile) > patches/09-shelf-sidebar.patch

with no check on git's exit status, so once `.git` was gone the step stopped
producing anything useful while every other part of the sync -- the ELF copy, the
shelf.c copy -- kept working and kept reporting success. `patches/09` therefore
sat six commits stale while the binary next to it was current.

So: `patches/09-shelf-sidebar.patch` is the diff and is regenerable; this
directory is the source of truth it is regenerated from. If the two disagree,
these files win, because the ELF is built from these.
