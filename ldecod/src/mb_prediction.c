/*!
 *************************************************************************************
 * \file mb_prediction.c
 *
 * \brief
 *    Macroblock prediction functions
 *
 * \author
 *    Main contributors (see contributors.h for copyright, address and affiliation details)
 *    - Alexis Michael Tourapis         <alexismt@ieee.org>
 *************************************************************************************
 */

#include "contributors.h"

#include "block.h"
#include "global.h"
#include "mbuffer.h"
#include "elements.h"
#include "macroblock.h"
#include "fmo.h"
#include "cabac.h"
#include "vlc.h"
#include "image.h"
#include "mb_access.h"
#include "biaridecod.h"
#include "transform8x8.h"
#include "transform.h"
#include "mc_prediction.h"
#include "quant.h"
#include "intra4x4_pred.h"
#include "intra8x8_pred.h"
#include "intra16x16_pred.h"
#include "mv_prediction.h"
#include "mb_prediction.h"

