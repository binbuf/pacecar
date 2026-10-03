#pragma once

// Thin convenience layer over WIL for the Pacecar core. Including this header gives consumers the
// shared RAII aliases in the `pacecar` namespace (in addition to the `wil::` originals) plus the
// result macros (THROW_IF_FAILED / RETURN_IF_FAILED / ...) used at HRESULT boundaries.

#include <wil/resource.h>
#include <wil/result_macros.h>

namespace pacecar
{
using wil::unique_handle;
using wil::unique_hfile;
using wil::unique_hwnd;
} // namespace pacecar