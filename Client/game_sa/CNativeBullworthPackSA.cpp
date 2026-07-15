/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeBullworthPackSA.cpp
 *  PURPOSE:     Reviewed Bullworth native world-pack descriptor
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeBullworthPackSA.h"
#include "CNativeWorldPackSA.h"

namespace
{
    constexpr const char* BULLWORTH_IPL_NAMES[] = {
        "bw_tbusines", "bw_tcarni", "bw_tglobal", "bw_tindust", "bw_tjyard", "bw_trich", "bw_tschool",
    };

    constexpr SNativeTxdPoolProfileSA BULLWORTH_TXD_POOL_PROFILES[] = {
        {"hoodlum-raw", "standalone-3607", 3607, 3606, -1, {true}},
        {"mta-programdata",
         "mta-runtime-3608",
         3608,
         3607,
         3607,
         {true, 0x01, 0x00000000, 0, 0xFFFF, 0xEA5A8E45, 0xFFFF, 0xFFFF, 0xFFFF, 0x00, 4, 13153, 5, 0}},
    };

    constexpr SNativeWorldPackDescriptorSA BULLWORTH_PACK = {
        "bullworth",
        "Bullworth",
        "[NativeBW]",
        "MTA_NATIVE_BW_MODEL_STORES",
        "MTA\\data\\extended-world\\bullworth",
        "bw.ide",
        "bw.img",
        "bw.col",
        "0bdf5aeb17eaefe6e2f42e47d38f82d65526c580f3eecc223b7b65f8b905eeb4",
        "bc7f3ad5ce47bbd8a9018c9743142582cd458875d2100f31c0d96aac7f4bbfc0",
        18631,
        19582,
        952,
        166,
        5000,
        252,
        255,
        191,
        256,
        BULLWORTH_IPL_NAMES,
        static_cast<unsigned int>(std::size(BULLWORTH_IPL_NAMES)),
        1126,
        82786,
        4007,
        6,
        {13984, 69, 160},
        {870, 67, 15},
        BULLWORTH_TXD_POOL_PROFILES,
        static_cast<unsigned int>(std::size(BULLWORTH_TXD_POOL_PROFILES)),
    };
}  // namespace

const SNativeWorldPackDescriptorSA& GetNativeBullworthPackDescriptor()
{
    return BULLWORTH_PACK;
}
