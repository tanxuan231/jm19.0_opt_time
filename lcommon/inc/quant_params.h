/*!
 ***************************************************************************
 * \file
 *    quant_params.h
 *
 * \author
 *    Alexis Michael Tourapis
 *
 * \brief
 *    Headerfile for Quantization parameters
 **************************************************************************
 */

#ifndef _QUANT_PARAMS_H_
#define _QUANT_PARAMS_H_

typedef struct level_quant_params {
  int   OffsetComp;
  int    ScaleComp;
  int InvScaleComp;
} LevelQuantParams;

#endif
