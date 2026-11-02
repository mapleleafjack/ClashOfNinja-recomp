#pragma once

// The util layer, in the four pieces it is built from: text, paths, processes and digests.
// Everything it ever declared is still here, so a file that includes util.h needs no change
// and there is one place to look for what the layer offers; a new file that only wants the
// digests can include crypto.h and pull in no more than that.
//
// The standard headers are kept for the files that were already leaning on them through
// this one.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "text.h"
#include "paths.h"
#include "proc.h"
#include "crypto.h"
