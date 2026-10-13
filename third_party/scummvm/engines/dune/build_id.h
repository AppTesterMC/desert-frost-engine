/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ENGINES_DUNE_BUILD_ID_H
#define ENGINES_DUNE_BUILD_ID_H

// Replaced in each staged build by scripts/scummvm_source.sh. Direct builds
// still show their compile date and time instead of an "unstamped" label.
#define DUNE_BUILD_ID "DUNE build " __DATE__ " " __TIME__ " #000000"

#endif
