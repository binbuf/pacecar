#include "pacecar/util/Pdh.h"

#include <wil/result_macros.h>

#pragma comment(lib, "pdh.lib")

namespace pacecar
{
void ThrowIfPdhError(PDH_STATUS status)
{
    if (status != ERROR_SUCCESS)
    {
        throw wil::ResultException(static_cast<HRESULT>(status));
    }
}
} // namespace pacecar