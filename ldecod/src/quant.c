
/*!
 ***********************************************************************
 *  \file
 *      quant.c
 *
 *  \brief
 *      Quantization functions
 *
 *  \author
 *      Main contributors (see contributors.h for copyright, address and affiliation details)
 *
 ***********************************************************************
 */

#include "contributors.h"

#include "global.h"
#include "memalloc.h"
#include "image.h"
#include "mb_access.h"
#include "transform.h"
#include "quant.h"

int quant_intra_default[16] = {
   6,13,20,28,
  13,20,28,32,
  20,28,32,37,
  28,32,37,42
};

int quant_inter_default[16] = {
  10,14,20,24,
  14,20,24,27,
  20,24,27,30,
  24,27,30,34
};

int quant8_intra_default[64] = {
 6,10,13,16,18,23,25,27,
10,11,16,18,23,25,27,29,
13,16,18,23,25,27,29,31,
16,18,23,25,27,29,31,33,
18,23,25,27,29,31,33,36,
23,25,27,29,31,33,36,38,
25,27,29,31,33,36,38,40,
27,29,31,33,36,38,40,42
};

int quant8_inter_default[64] = {
 9,13,15,17,19,21,22,24,
13,13,17,19,21,22,24,25,
15,17,19,21,22,24,25,27,
17,19,21,22,24,25,27,28,
19,21,22,24,25,27,28,30,
21,22,24,25,27,28,30,32,
22,24,25,27,28,30,32,33,
24,25,27,28,30,32,33,35
};

int quant_org[16] = { //to be use if no q matrix is chosen
16,16,16,16,
16,16,16,16,
16,16,16,16,
16,16,16,16
};

int quant8_org[64] = { //to be use if no q matrix is chosen
16,16,16,16,16,16,16,16,
16,16,16,16,16,16,16,16,
16,16,16,16,16,16,16,16,
16,16,16,16,16,16,16,16,
16,16,16,16,16,16,16,16,
16,16,16,16,16,16,16,16,
16,16,16,16,16,16,16,16,
16,16,16,16,16,16,16,16
};

/*!
 ***********************************************************************
 * \brief
 *    Initiate quantization process arrays
 ***********************************************************************
 */
void init_qp_process(CodingParameters *cps)
{
  int bitdepth_qp_scale = imax(cps->bitdepth_luma_qp_scale, cps->bitdepth_chroma_qp_scale);
  int i;

  // We should allocate memory outside of this process since maybe we will have a change of SPS 
  // and we may need to recreate these. Currently should only support same bitdepth
  if (cps->qp_per_matrix == NULL)
    if ((cps->qp_per_matrix = (int*)malloc((MAX_QP + 1 +  bitdepth_qp_scale)*sizeof(int))) == NULL)
      no_mem_exit("init_qp_process: cps->qp_per_matrix");

  if (cps->qp_rem_matrix == NULL)
    if ((cps->qp_rem_matrix = (int*)malloc((MAX_QP + 1 +  bitdepth_qp_scale)*sizeof(int))) == NULL)
      no_mem_exit("init_qp_process: cps->qp_rem_matrix");

  for (i = 0; i < MAX_QP + bitdepth_qp_scale + 1; i++)
  {
    cps->qp_per_matrix[i] = i / 6;
    cps->qp_rem_matrix[i] = i % 6;
  }
}

void free_qp_matrices(CodingParameters *cps)
{
  if (cps->qp_per_matrix != NULL)
  {
    free (cps->qp_per_matrix);
    cps->qp_per_matrix = NULL;
  }

  if (cps->qp_rem_matrix != NULL)
  {
    free (cps->qp_rem_matrix);
    cps->qp_rem_matrix = NULL;
  }
}
