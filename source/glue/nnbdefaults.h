#pragma once
//
// Defaults shared by every game in the program.
//

// The public lobby (masterserver). A game reports its internet matches here and
// asks here for the list of matches, unless another lobby is named with
// cl_masterserver, --masteraddr, --advertise and the like.
#define NNB_DEFAULT_MASTERSERVER "nnb.ms.nukemnet.com"
