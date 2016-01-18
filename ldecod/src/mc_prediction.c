
/*!
 *************************************************************************************
 * \file mc_prediction.c
 *
 * \brief
 *    Functions for motion compensated prediction
 *
 * \author
 *      Main contributors (see contributors.h for copyright, 
 *                         address and affiliation details)
 *      - Alexis Michael Tourapis  <alexismt@ieee.org>
 *      - Chris Vogt
 *
 *************************************************************************************
 */
#include "global.h"
#include "block.h"
#include "mc_prediction.h"
#include "mbuffer.h"
#include "mb_access.h"
#include "macroblock.h"
#include "memalloc.h"
#include "dec_statistics.h"

int allocate_pred_mem(Slice *currSlice)
{
  int alloc_size = 0;
  alloc_size += get_mem2Dpel(&currSlice->tmp_block_l0, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
  alloc_size += get_mem2Dpel(&currSlice->tmp_block_l1, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
  alloc_size += get_mem2Dpel(&currSlice->tmp_block_l2, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
  alloc_size += get_mem2Dpel(&currSlice->tmp_block_l3, MB_BLOCK_SIZE, MB_BLOCK_SIZE);
  alloc_size += get_mem2Dint(&currSlice->tmp_res, MB_BLOCK_SIZE + 5, MB_BLOCK_SIZE + 5);
  return (alloc_size);
}

void free_pred_mem(Slice *currSlice)
{
  free_mem2Dint(currSlice->tmp_res);
  free_mem2Dpel(currSlice->tmp_block_l0);
  free_mem2Dpel(currSlice->tmp_block_l1);
  free_mem2Dpel(currSlice->tmp_block_l2);
  free_mem2Dpel(currSlice->tmp_block_l3);
}

static const int COEF[6] = { 1, -5, 20, 20, -5, 1 };

/*!
 ************************************************************************
 * \brief
 *    block single list prediction
 ************************************************************************
 */
static void mc_prediction(imgpel **mb_pred, imgpel **block, int block_size_y, int block_size_x, int ioff)
{  

  int j;

  for (j = 0; j < block_size_y; j++)
  {
    memcpy(&mb_pred[j][ioff], block[j], block_size_x * sizeof(imgpel));
  }
}

/*!
 ************************************************************************
 * \brief
 *    block single list weighted prediction
 ************************************************************************
 */
static void weighted_mc_prediction(imgpel **mb_pred, 
                                   imgpel **block, 
                                   int block_size_y, 
                                   int block_size_x, 
                                   int ioff,
                                   int wp_scale,
                                   int wp_offset,
                                   int weight_denom,
                                   int color_clip)
{
  int i, j;
  int result;

  for(j = 0; j < block_size_y; j++) 
  {
    for(i = 0; i < block_size_x; i++) 
    {
      result = rshift_rnd((wp_scale * block[j][i]), weight_denom) + wp_offset;      
      mb_pred[j][i + ioff] = (imgpel)iClip3(0, color_clip, result);
    }
  }
}



/*!
 ************************************************************************
 * \brief
 *    block bi-prediction
 ************************************************************************
 */
static void bi_prediction(imgpel **mb_pred, 
                          imgpel **block_l0, 
                          imgpel **block_l1,
                          int block_size_y, 
                          int block_size_x,
                          int ioff)
{
  imgpel *mpr = &mb_pred[0][ioff];
  imgpel *b0 = block_l0[0];
  imgpel *b1 = block_l1[0];
  int ii, jj;
  int row_inc = MB_BLOCK_SIZE - block_size_x;
  for(jj = 0;jj < block_size_y;jj++)
  {
    // unroll the loop 
    for(ii = 0; ii < block_size_x; ii += 2) 
    {
      *(mpr++) = (imgpel)(((*(b0++) + *(b1++)) + 1) >> 1);
      *(mpr++) = (imgpel)(((*(b0++) + *(b1++)) + 1) >> 1);
    }
    mpr += row_inc;
    b0  += row_inc;
    b1  += row_inc;
  }
}



/*!
 ************************************************************************
 * \brief
 *    block weighted biprediction
 ************************************************************************
 */
static void weighted_bi_prediction(imgpel *mb_pred, 
                                   imgpel *block_l0, 
                                   imgpel *block_l1, 
                                   int block_size_y, 
                                   int block_size_x, 
                                   int wp_scale_l0, 
                                   int wp_scale_l1, 
                                   int wp_offset, 
                                   int weight_denom, 
                                   int color_clip)
{
  int i, j, result;
  int row_inc = MB_BLOCK_SIZE - block_size_x;

  for(j = 0; j < block_size_y; j++)
  {
    for(i = 0; i < block_size_x; i++) 
    {
      result = rshift_rnd_sf((wp_scale_l0 * *(block_l0++) + wp_scale_l1 * *(block_l1++)),  weight_denom);

      *(mb_pred++) = (imgpel) iClip1(color_clip, result + wp_offset);
    }
    mb_pred += row_inc;
    block_l0 += row_inc;
    block_l1 += row_inc;
  }
}

/*!
 ************************************************************************
 * \brief
 *    Integer positions
 ************************************************************************
 */ 
static void get_block_00(imgpel *block, imgpel *cur_img, int span, int block_size_y)
{
  // fastest to just move an entire block, since block is a temp block is a 256 byte block (16x16)
  // writes 2 lines of 16 imgpel 1 to 8 times depending in block_size_y
  int j;
  
  for (j = 0; j < block_size_y; j += 2)
  { 
    memcpy(block, cur_img, MB_BLOCK_SIZE * sizeof(imgpel));
    block += MB_BLOCK_SIZE;
    cur_img += span;
    memcpy(block, cur_img, MB_BLOCK_SIZE * sizeof(imgpel));
    block += MB_BLOCK_SIZE;
    cur_img += span;
  }
}


/*!
 ************************************************************************
 * \brief
 *    Qpel (1,0) horizontal
 ************************************************************************
 */ 
static void get_luma_10(imgpel **block, imgpel **cur_imgY, int block_size_y, int block_size_x, int x_pos , int max_imgpel_value)
{
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  imgpel *orig_line, *cur_line;
  int i, j;
  int result;
  
  for (j = 0; j < block_size_y; j++)
  {
    cur_line = &(cur_imgY[j][x_pos]);
    p0 = &cur_imgY[j][x_pos - 2];
    p1 = p0 + 1;
    p2 = p1 + 1;
    p3 = p2 + 1;
    p4 = p3 + 1;
    p5 = p4 + 1;
    orig_line = block[j];            

    for (i = 0; i < block_size_x; i++)
    {        
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *orig_line = (imgpel) iClip1(max_imgpel_value, ((result + 16)>>5));
      *orig_line = (imgpel) ((*orig_line + *(cur_line++) + 1 ) >> 1);
      orig_line++;
    }
  }
}

/*!
 ************************************************************************
 * \brief
 *    Half horizontal
 ************************************************************************
 */ 
static void get_luma_20(imgpel **block, imgpel **cur_imgY, int block_size_y, int block_size_x, int x_pos , int max_imgpel_value)
{
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  imgpel *orig_line;
  int i, j;
  int result;
  for (j = 0; j < block_size_y; j++)
  {
    p0 = &cur_imgY[j][x_pos - 2];
    p1 = p0 + 1;
    p2 = p1 + 1;
    p3 = p2 + 1;
    p4 = p3 + 1;
    p5 = p4 + 1;
    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {        
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *orig_line++ = (imgpel) iClip1(max_imgpel_value, ((result + 16)>>5));
    }
  }
}

/*!
 ************************************************************************
 * \brief
 *    Qpel (3,0) horizontal
 ************************************************************************
 */ 
static void get_luma_30(imgpel **block, imgpel **cur_imgY, int block_size_y, int block_size_x, int x_pos , int max_imgpel_value)
{
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  imgpel *orig_line, *cur_line;
  int i, j;
  int result;
  
  for (j = 0; j < block_size_y; j++)
  {
    cur_line = &(cur_imgY[j][x_pos + 1]);
    p0 = &cur_imgY[j][x_pos - 2];
    p1 = p0 + 1;
    p2 = p1 + 1;
    p3 = p2 + 1;
    p4 = p3 + 1;
    p5 = p4 + 1;
    orig_line = block[j];            

    for (i = 0; i < block_size_x; i++)
    {        
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *orig_line = (imgpel) iClip1(max_imgpel_value, ((result + 16)>>5));
      *orig_line = (imgpel) ((*orig_line + *(cur_line++) + 1 ) >> 1);
      orig_line++;
    }
  }
}

/*!
 ************************************************************************
 * \brief
 *    Qpel vertical (0, 1)
 ************************************************************************
 */ 
static void get_luma_01(imgpel **block, imgpel **cur_imgY, int block_size_y, int block_size_x, int x_pos, int shift_x, int max_imgpel_value)
{
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  imgpel *orig_line, *cur_line;
  int i, j;
  int result;
  int jj = 0;
  p0 = &(cur_imgY[ - 2][x_pos]);
  for (j = 0; j < block_size_y; j++)
  {                  
    p1 = p0 + shift_x;          
    p2 = p1 + shift_x;
    p3 = p2 + shift_x;
    p4 = p3 + shift_x;
    p5 = p4 + shift_x;
    orig_line = block[j];
    cur_line = &(cur_imgY[jj++][x_pos]);

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *orig_line = (imgpel) iClip1(max_imgpel_value, ((result + 16)>>5));
      *orig_line = (imgpel) ((*orig_line + *(cur_line++) + 1 ) >> 1);
      orig_line++;
    }
    p0 = p1 - block_size_x;
  }
}


/*!
 ************************************************************************
 * \brief
 *    Half vertical
 ************************************************************************
 */ 
static void get_luma_02(imgpel **block, imgpel **cur_imgY, int block_size_y, int block_size_x, int x_pos, int shift_x, int max_imgpel_value)
{
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  imgpel *orig_line;
  int i, j;
  int result;
  p0 = &(cur_imgY[ - 2][x_pos]);
  for (j = 0; j < block_size_y; j++)
  {                  
    p1 = p0 + shift_x;          
    p2 = p1 + shift_x;
    p3 = p2 + shift_x;
    p4 = p3 + shift_x;
    p5 = p4 + shift_x;
    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *orig_line++ = (imgpel) iClip1(max_imgpel_value, ((result + 16)>>5));
    }
    p0 = p1 - block_size_x;
  }
}


/*!
 ************************************************************************
 * \brief
 *    Qpel vertical (0, 3)
 ************************************************************************
 */ 
static void get_luma_03(imgpel **block, imgpel **cur_imgY, int block_size_y, int block_size_x, int x_pos, int shift_x, int max_imgpel_value)
{
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  imgpel *orig_line, *cur_line;
  int i, j;
  int result;
  int jj = 1;

  p0 = &(cur_imgY[ -2][x_pos]);
  for (j = 0; j < block_size_y; j++)
  {                  
    p1 = p0 + shift_x;          
    p2 = p1 + shift_x;
    p3 = p2 + shift_x;
    p4 = p3 + shift_x;
    p5 = p4 + shift_x;
    orig_line = block[j];
    cur_line = &(cur_imgY[jj++][x_pos]);

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *orig_line = (imgpel) iClip1(max_imgpel_value, ((result + 16)>>5));
      *orig_line = (imgpel) ((*orig_line + *(cur_line++) + 1 ) >> 1);
      orig_line++;
    }
    p0 = p1 - block_size_x;
  }
}

/*!
 ************************************************************************
 * \brief
 *    Hpel horizontal, Qpel vertical (2, 1)
 ************************************************************************
 */ 
static void get_luma_21(imgpel **block, imgpel **cur_imgY, int **tmp_res, int block_size_y, int block_size_x, int x_pos, int max_imgpel_value)
{
  int i, j;
  /* Vertical & horizontal interpolation */
  int *tmp_line;
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  int    *x0, *x1, *x2, *x3, *x4, *x5;  
  imgpel *orig_line;  
  int result;      

  int jj = -2;

  for (j = 0; j < block_size_y + 5; j++)
  {
    p0 = &cur_imgY[jj++][x_pos - 2];
    p1 = p0 + 1;
    p2 = p1 + 1;
    p3 = p2 + 1;
    p4 = p3 + 1;
    p5 = p4 + 1;          
    tmp_line  = tmp_res[j];

    for (i = 0; i < block_size_x; i++)
    {        
      *(tmp_line++) = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));
    }
  }  

  jj = 2;
  for (j = 0; j < block_size_y; j++)
  {
    tmp_line  = tmp_res[jj++];
    x0 = tmp_res[j    ];
    x1 = tmp_res[j + 1];
    x2 = tmp_res[j + 2];
    x3 = tmp_res[j + 3];
    x4 = tmp_res[j + 4];
    x5 = tmp_res[j + 5];
    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*x0++ + *x5++) - 5 * (*x1++ + *x4++) + 20 * (*x2++ + *x3++);

      *orig_line = (imgpel) iClip1(max_imgpel_value, ((result + 512)>>10));
      *orig_line = (imgpel) ((*orig_line + iClip1(max_imgpel_value, ((*(tmp_line++) + 16) >> 5)) + 1 )>> 1);
      orig_line++;
    }
  }
}

/*!
 ************************************************************************
 * \brief
 *    Hpel horizontal, Hpel vertical (2, 2)
 ************************************************************************
 */ 
static void get_luma_22(imgpel **block, imgpel **cur_imgY, int **tmp_res, int block_size_y, int block_size_x, int x_pos, int max_imgpel_value)
{
  int i, j;
  /* Vertical & horizontal interpolation */
  int *tmp_line;
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  int    *x0, *x1, *x2, *x3, *x4, *x5;  
  imgpel *orig_line;  
  int result;      

  int jj = - 2;

  for (j = 0; j < block_size_y + 5; j++)
  {
    p0 = &cur_imgY[jj++][x_pos - 2];
    p1 = p0 + 1;
    p2 = p1 + 1;
    p3 = p2 + 1;
    p4 = p3 + 1;
    p5 = p4 + 1;          
    tmp_line  = tmp_res[j];

    for (i = 0; i < block_size_x; i++)
    {        
      *(tmp_line++) = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));
    }
  }

  for (j = 0; j < block_size_y; j++)
  {
    x0 = tmp_res[j    ];
    x1 = tmp_res[j + 1];
    x2 = tmp_res[j + 2];
    x3 = tmp_res[j + 3];
    x4 = tmp_res[j + 4];
    x5 = tmp_res[j + 5];
    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*x0++ + *x5++) - 5 * (*x1++ + *x4++) + 20 * (*x2++ + *x3++);

      *(orig_line++) = (imgpel) iClip1(max_imgpel_value, ((result + 512)>>10));
    }
  }
}

/*!
 ************************************************************************
 * \brief
 *    Hpel horizontal, Qpel vertical (2, 3)
 ************************************************************************
 */ 
static void get_luma_23(imgpel **block, imgpel **cur_imgY, int **tmp_res, int block_size_y, int block_size_x, int x_pos, int max_imgpel_value)
{
  int i, j;
  /* Vertical & horizontal interpolation */
  int *tmp_line;
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  int    *x0, *x1, *x2, *x3, *x4, *x5;  
  imgpel *orig_line;  
  int result;      

  int jj = -2;

  for (j = 0; j < block_size_y + 5; j++)
  {
    p0 = &cur_imgY[jj++][x_pos - 2];
    p1 = p0 + 1;
    p2 = p1 + 1;
    p3 = p2 + 1;
    p4 = p3 + 1;
    p5 = p4 + 1;          
    tmp_line  = tmp_res[j];

    for (i = 0; i < block_size_x; i++)
    {        
      *(tmp_line++) = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));
    }
  }

  jj = 3;
  for (j = 0; j < block_size_y; j++)
  {
    tmp_line  = tmp_res[jj++];
    x0 = tmp_res[j    ];
    x1 = tmp_res[j + 1];
    x2 = tmp_res[j + 2];
    x3 = tmp_res[j + 3];
    x4 = tmp_res[j + 4];
    x5 = tmp_res[j + 5];
    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*x0++ + *x5++) - 5 * (*x1++ + *x4++) + 20 * (*x2++ + *x3++);

      *orig_line = (imgpel) iClip1(max_imgpel_value, ((result + 512)>>10));
      *orig_line = (imgpel) ((*orig_line + iClip1(max_imgpel_value, ((*(tmp_line++) + 16) >> 5)) + 1 )>> 1);
      orig_line++;
    }
  }
}

/*!
 ************************************************************************
 * \brief
 *    Qpel horizontal, Hpel vertical (1, 2)
 ************************************************************************
 */ 
static void get_luma_12(imgpel **block, imgpel **cur_imgY, int **tmp_res, int block_size_y, int block_size_x, int x_pos, int shift_x, int max_imgpel_value)
{
  int i, j;
  int *tmp_line;
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;        
  int    *x0, *x1, *x2, *x3, *x4, *x5;  
  imgpel *orig_line;  
  int result;      

  p0 = &(cur_imgY[ -2][x_pos - 2]);
  for (j = 0; j < block_size_y; j++)
  {                    
    p1 = p0 + shift_x;
    p2 = p1 + shift_x;
    p3 = p2 + shift_x;
    p4 = p3 + shift_x;
    p5 = p4 + shift_x;
    tmp_line  = tmp_res[j];

    for (i = 0; i < block_size_x + 5; i++)
    {
      *(tmp_line++)  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));
    }
    p0 = p1 - (block_size_x + 5);
  }

  for (j = 0; j < block_size_y; j++)
  {
    tmp_line  = &tmp_res[j][2];
    orig_line = block[j];
    x0 = tmp_res[j];
    x1 = x0 + 1;
    x2 = x1 + 1;
    x3 = x2 + 1;
    x4 = x3 + 1;
    x5 = x4 + 1;

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*(x0++) + *(x5++)) - 5 * (*(x1++) + *(x4++)) + 20 * (*(x2++) + *(x3++));

      *orig_line = (imgpel) iClip1(max_imgpel_value, ((result + 512)>>10));
      *orig_line = (imgpel) ((*orig_line + iClip1(max_imgpel_value, ((*(tmp_line++) + 16)>>5))+1)>>1);
      orig_line ++;
    }
  }  
}


/*!
 ************************************************************************
 * \brief
 *    Qpel horizontal, Hpel vertical (3, 2)
 ************************************************************************
 */ 
static void get_luma_32(imgpel **block, imgpel **cur_imgY, int **tmp_res, int block_size_y, int block_size_x, int x_pos, int shift_x, int max_imgpel_value)
{
  int i, j;
  int *tmp_line;
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;        
  int    *x0, *x1, *x2, *x3, *x4, *x5;  
  imgpel *orig_line;  
  int result;      

  p0 = &(cur_imgY[ -2][x_pos - 2]);
  for (j = 0; j < block_size_y; j++)
  {                    
    p1 = p0 + shift_x;
    p2 = p1 + shift_x;
    p3 = p2 + shift_x;
    p4 = p3 + shift_x;
    p5 = p4 + shift_x;
    tmp_line  = tmp_res[j];

    for (i = 0; i < block_size_x + 5; i++)
    {
      *(tmp_line++)  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));
    }
    p0 = p1 - (block_size_x + 5);
  }

  for (j = 0; j < block_size_y; j++)
  {
    tmp_line  = &tmp_res[j][3];
    orig_line = block[j];
    x0 = tmp_res[j];
    x1 = x0 + 1;
    x2 = x1 + 1;
    x3 = x2 + 1;
    x4 = x3 + 1;
    x5 = x4 + 1;

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*(x0++) + *(x5++)) - 5 * (*(x1++) + *(x4++)) + 20 * (*(x2++) + *(x3++));

      *orig_line = (imgpel) iClip1(max_imgpel_value, ((result + 512)>>10));
      *orig_line = (imgpel) ((*orig_line + iClip1(max_imgpel_value, ((*(tmp_line++) + 16)>>5))+1)>>1);
      orig_line ++;
    }
  }
}

/*!
 ************************************************************************
 * \brief
 *    Qpel horizontal, Qpel vertical (3, 3)
 ************************************************************************
 */ 
static void get_luma_33(imgpel **block, imgpel **cur_imgY, int block_size_y, int block_size_x, int x_pos, int shift_x, int max_imgpel_value)
{
  int i, j;
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  imgpel *orig_line;  
  int result;      

  int jj = 1;

  for (j = 0; j < block_size_y; j++)
  {
    p0 = &cur_imgY[jj++][x_pos - 2];
    p1 = p0 + 1;
    p2 = p1 + 1;
    p3 = p2 + 1;
    p4 = p3 + 1;
    p5 = p4 + 1;

    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {        
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *(orig_line++) = (imgpel) iClip1(max_imgpel_value, ((result + 16)>>5));
    }
  }

  p0 = &(cur_imgY[-2][x_pos + 1]);
  for (j = 0; j < block_size_y; j++)
  {        
    p1 = p0 + shift_x;
    p2 = p1 + shift_x;
    p3 = p2 + shift_x;
    p4 = p3 + shift_x;
    p5 = p4 + shift_x;
    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *orig_line = (imgpel) ((*orig_line + iClip1(max_imgpel_value, ((result + 16) >> 5)) + 1) >> 1);
      orig_line++;
    }
    p0 = p1 - block_size_x ;
  }      
}



/*!
 ************************************************************************
 * \brief
 *    Qpel horizontal, Qpel vertical (1, 1)
 ************************************************************************
 */ 
static void get_luma_11(imgpel **block, imgpel **cur_imgY, int block_size_y, int block_size_x, int x_pos, int shift_x, int max_imgpel_value)
{
  int i, j;
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  imgpel *orig_line;  
  int result;      

  int jj = 0;

  for (j = 0; j < block_size_y; j++)
  {
    p0 = &cur_imgY[jj++][x_pos - 2];
    p1 = p0 + 1;
    p2 = p1 + 1;
    p3 = p2 + 1;
    p4 = p3 + 1;
    p5 = p4 + 1;

    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {        
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *(orig_line++) = (imgpel) iClip1(max_imgpel_value, ((result + 16)>>5));
    }
  }

  p0 = &(cur_imgY[-2][x_pos]);
  for (j = 0; j < block_size_y; j++)
  {        
    p1 = p0 + shift_x;
    p2 = p1 + shift_x;
    p3 = p2 + shift_x;
    p4 = p3 + shift_x;
    p5 = p4 + shift_x;
    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *orig_line = (imgpel) ((*orig_line + iClip1(max_imgpel_value, ((result + 16) >> 5)) + 1) >> 1);
      orig_line++;
    }
    p0 = p1 - block_size_x ;
  }      
}

/*!
 ************************************************************************
 * \brief
 *    Qpel horizontal, Qpel vertical (1, 3)
 ************************************************************************
 */ 
static void get_luma_13(imgpel **block, imgpel **cur_imgY, int block_size_y, int block_size_x, int x_pos, int shift_x, int max_imgpel_value)
{
  /* Diagonal interpolation */
  int i, j;
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  imgpel *orig_line;  
  int result;      

  int jj = 1;

  for (j = 0; j < block_size_y; j++)
  {
    p0 = &cur_imgY[jj++][x_pos - 2];
    p1 = p0 + 1;
    p2 = p1 + 1;
    p3 = p2 + 1;
    p4 = p3 + 1;
    p5 = p4 + 1;

    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {        
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *(orig_line++) = (imgpel) iClip1(max_imgpel_value, ((result + 16)>>5));
    }
  }

  p0 = &(cur_imgY[-2][x_pos]);
  for (j = 0; j < block_size_y; j++)
  {        
    p1 = p0 + shift_x;
    p2 = p1 + shift_x;
    p3 = p2 + shift_x;
    p4 = p3 + shift_x;
    p5 = p4 + shift_x;
    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *orig_line = (imgpel) ((*orig_line + iClip1(max_imgpel_value, ((result + 16) >> 5)) + 1) >> 1);
      orig_line++;
    }
    p0 = p1 - block_size_x ;
  }      
}

/*!
 ************************************************************************
 * \brief
 *    Qpel horizontal, Qpel vertical (3, 1)
 ************************************************************************
 */ 
static void get_luma_31(imgpel **block, imgpel **cur_imgY, int block_size_y, int block_size_x, int x_pos, int shift_x, int max_imgpel_value)
{
  /* Diagonal interpolation */
  int i, j;
  imgpel *p0, *p1, *p2, *p3, *p4, *p5;
  imgpel *orig_line;  
  int result;      

  int jj = 0;

  for (j = 0; j < block_size_y; j++)
  {
    p0 = &cur_imgY[jj++][x_pos - 2];
    p1 = p0 + 1;
    p2 = p1 + 1;
    p3 = p2 + 1;
    p4 = p3 + 1;
    p5 = p4 + 1;

    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {        
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *(orig_line++) = (imgpel) iClip1(max_imgpel_value, ((result + 16)>>5));
    }
  }

  p0 = &(cur_imgY[-2][x_pos + 1]);
  for (j = 0; j < block_size_y; j++)
  {        
    p1 = p0 + shift_x;
    p2 = p1 + shift_x;
    p3 = p2 + shift_x;
    p4 = p3 + shift_x;
    p5 = p4 + shift_x;
    orig_line = block[j];

    for (i = 0; i < block_size_x; i++)
    {
      result  = (*(p0++) + *(p5++)) - 5 * (*(p1++) + *(p4++)) + 20 * (*(p2++) + *(p3++));

      *orig_line = (imgpel) ((*orig_line + iClip1(max_imgpel_value, ((result + 16) >> 5)) + 1) >> 1);
      orig_line++;
    }
    p0 = p1 - block_size_x ;
  }      
}

/*!
 ************************************************************************
 * \brief
 *    Interpolation of 1/4 subpixel
 ************************************************************************
 */ 
void get_block_luma(StorablePicture *curr_ref, int x_pos, int y_pos, int block_size_x, int block_size_y, imgpel **block,
                    int shift_x, int maxold_x, int maxold_y, int **tmp_res, int max_imgpel_value, imgpel no_ref_value, Macroblock *currMB)
{
  if (curr_ref->no_ref) {
    //printf("list[ref_frame] is equal to 'no reference picture' before RAP\n");
    memset(block[0],no_ref_value,block_size_y * block_size_x * sizeof(imgpel));
  }
  else
  {
    imgpel **cur_imgY = (currMB->p_Vid->separate_colour_plane_flag && currMB->p_Slice->colour_plane_id>PLANE_Y)? curr_ref->imgUV[currMB->p_Slice->colour_plane_id-1] : curr_ref->cur_imgY;
    int dx = (x_pos & 3);
    int dy = (y_pos & 3);
    x_pos >>= 2;
    y_pos >>= 2;
    x_pos = iClip3(-18, maxold_x+2, x_pos);
    y_pos = iClip3(-10, maxold_y+2, y_pos);

    if (dx == 0 && dy == 0)
      get_block_00(&block[0][0], &cur_imgY[y_pos][x_pos], curr_ref->iLumaStride, block_size_y);
    else
    { /* other positions */
      if (dy == 0) /* No vertical interpolation */
      {         
        if (dx == 1)
          get_luma_10(block, &cur_imgY[ y_pos], block_size_y, block_size_x, x_pos, max_imgpel_value);
        else if (dx == 2)
          get_luma_20(block, &cur_imgY[ y_pos], block_size_y, block_size_x, x_pos, max_imgpel_value);
        else
          get_luma_30(block, &cur_imgY[ y_pos], block_size_y, block_size_x, x_pos, max_imgpel_value);
      }
      else if (dx == 0) /* No horizontal interpolation */        
      {         
        if (dy == 1)
          get_luma_01(block, &cur_imgY[y_pos], block_size_y, block_size_x, x_pos, shift_x, max_imgpel_value);
        else if (dy == 2)
          get_luma_02(block, &cur_imgY[ y_pos], block_size_y, block_size_x, x_pos, shift_x, max_imgpel_value);
        else
          get_luma_03(block, &cur_imgY[ y_pos], block_size_y, block_size_x, x_pos, shift_x, max_imgpel_value);
      }
      else if (dx == 2)  /* Vertical & horizontal interpolation */
      {  
        if (dy == 1)
          get_luma_21(block, &cur_imgY[ y_pos], tmp_res, block_size_y, block_size_x, x_pos, max_imgpel_value);
        else if (dy == 2)
          get_luma_22(block, &cur_imgY[ y_pos], tmp_res, block_size_y, block_size_x, x_pos, max_imgpel_value);
        else
          get_luma_23(block, &cur_imgY[ y_pos], tmp_res, block_size_y, block_size_x, x_pos, max_imgpel_value);
      }
      else if (dy == 2)
      {
        if (dx == 1)
          get_luma_12(block, &cur_imgY[ y_pos], tmp_res, block_size_y, block_size_x, x_pos, shift_x, max_imgpel_value);
        else
          get_luma_32(block, &cur_imgY[ y_pos], tmp_res, block_size_y, block_size_x, x_pos, shift_x, max_imgpel_value);
      }
      else
      {
        if (dx == 1)
        {
          if (dy == 1)
            get_luma_11(block, &cur_imgY[ y_pos], block_size_y, block_size_x, x_pos, shift_x, max_imgpel_value);
          else
            get_luma_13(block, &cur_imgY[ y_pos], block_size_y, block_size_x, x_pos, shift_x, max_imgpel_value);
        }
        else
        {
          if (dy == 1)
            get_luma_31(block, &cur_imgY[ y_pos], block_size_y, block_size_x, x_pos, shift_x, max_imgpel_value);
          else
            get_luma_33(block, &cur_imgY[ y_pos], block_size_y, block_size_x, x_pos, shift_x, max_imgpel_value);
        }
      }
    }
  }
}


/*!
 ************************************************************************
 * \brief
 *    Chroma (0,X)
 ************************************************************************
 */ 
static void get_chroma_0X(imgpel *block, imgpel *cur_img, int span, int block_size_y, int block_size_x, int w00, int w01, int total_scale)
{
  imgpel *cur_row = cur_img;
  imgpel *nxt_row = cur_img + span;


  imgpel *cur_line, *cur_line_p1;
  imgpel *blk_line;
  int result;
  int i, j;
  for (j = 0; j < block_size_y; j++)
  {
      cur_line    = cur_row;
      cur_line_p1 = nxt_row;
      blk_line = block;
      block += 16;
      cur_row = nxt_row;
      nxt_row += span;
    for (i = 0; i < block_size_x; i++)
    {
      result = (w00 * *cur_line++ + w01 * *cur_line_p1++);
      *(blk_line++) = (imgpel) rshift_rnd_sf(result, total_scale);
    }
  }
}


/*!
 ************************************************************************
 * \brief
 *    Chroma (X,0)
 ************************************************************************
 */ 
static void get_chroma_X0(imgpel *block, imgpel *cur_img, int span, int block_size_y, int block_size_x, int w00, int w10, int total_scale)
{
  imgpel *cur_row = cur_img;
 

    imgpel *cur_line, *cur_line_p1;
    imgpel *blk_line;
    int result;
    int i, j;
    for (j = 0; j < block_size_y; j++)
    {
      cur_line    = cur_row;
      cur_line_p1 = cur_line + 1;
      blk_line = block;
      block += 16;
      cur_row += span;
      for (i = 0; i < block_size_x; i++)
      {
        result = (w00 * *cur_line++ + w10 * *cur_line_p1++);
        //*(blk_line++) = (imgpel) iClip1(max_imgpel_value, rshift_rnd_sf(result, total_scale));
        *(blk_line++) = (imgpel) rshift_rnd_sf(result, total_scale);
      }
    }
}

/*!
 ************************************************************************
 * \brief
 *    Chroma (X,X)
 ************************************************************************
 */ 
static void get_chroma_XY(imgpel *block, imgpel *cur_img, int span, int block_size_y, int block_size_x, int w00, int w01, int w10, int w11, int total_scale)
{ 
  imgpel *cur_row = cur_img;
  imgpel *nxt_row = cur_img + span;


  {
    imgpel *cur_line, *cur_line_p1;
    imgpel *blk_line;
    int result;
    int i, j;
    for (j = 0; j < block_size_y; j++)
    {
      cur_line    = cur_row;
      cur_line_p1 = nxt_row;
      blk_line = block;
      block += 16;
      cur_row = nxt_row;
      nxt_row += span;
      for (i = 0; i < block_size_x; i++)
      {
        result  = (w00 * *(cur_line++) + w01 * *(cur_line_p1++));
        result += (w10 * *(cur_line  ) + w11 * *(cur_line_p1  ));
        *(blk_line++) = (imgpel) rshift_rnd_sf(result, total_scale);
      }
    }
  }
}

static void get_block_chroma(StorablePicture *curr_ref, int x_pos, int y_pos, int subpel_x, int subpel_y, int maxold_x, int maxold_y,
                             int block_size_x, int vert_block_size, int shiftpel_x, int shiftpel_y,
                             imgpel *block1, imgpel *block2, int total_scale, imgpel no_ref_value, VideoParameters *p_Vid)
{
  imgpel *img1,*img2;
  short dx,dy;
  int span = curr_ref->iChromaStride;
  if (curr_ref->no_ref) {
    //printf("list[ref_frame] is equal to 'no reference picture' before RAP\n");
    memset(block1,no_ref_value,vert_block_size * block_size_x * sizeof(imgpel));
    memset(block2,no_ref_value,vert_block_size * block_size_x * sizeof(imgpel));
  }
  else
  {
    dx = (short) (x_pos & subpel_x);
    dy = (short) (y_pos & subpel_y);
    x_pos = x_pos >> shiftpel_x;
    y_pos = y_pos >> shiftpel_y;
    //clip MV;
    assert(vert_block_size <=p_Vid->iChromaPadY && block_size_x<=p_Vid->iChromaPadX);
    x_pos = iClip3(-p_Vid->iChromaPadX, maxold_x, x_pos); //16
    y_pos = iClip3(-p_Vid->iChromaPadY, maxold_y, y_pos); //8
    img1 = &curr_ref->imgUV[0][y_pos][x_pos];
    img2 = &curr_ref->imgUV[1][y_pos][x_pos];

    if (dx == 0 && dy == 0) 
    {
      get_block_00(block1, img1, span, vert_block_size);
      get_block_00(block2, img2, span, vert_block_size);
    }
    else 
    {
      short dxcur = (short) (subpel_x + 1 - dx);
      short dycur = (short) (subpel_y + 1 - dy);
      short w00 = dxcur * dycur;
      if (dx == 0)
      {
        short w01 = dxcur * dy;
        get_chroma_0X(block1, img1, span, vert_block_size, block_size_x, w00, w01, total_scale);
        get_chroma_0X(block2, img2, span, vert_block_size, block_size_x, w00, w01, total_scale);
      }
      else if (dy == 0)
      {
        short w10 = dx * dycur;
        get_chroma_X0(block1, img1, span, vert_block_size, block_size_x, w00, w10, total_scale);
        get_chroma_X0(block2, img2, span, vert_block_size, block_size_x, w00, w10, total_scale);
      }
      else
      {
        short w01 = dxcur * dy;
        short w10 = dx * dycur;
        short w11 = dx * dy;
        get_chroma_XY(block1, img1, span, vert_block_size, block_size_x, w00, w01, w10, w11, total_scale);
        get_chroma_XY(block2, img2, span, vert_block_size, block_size_x, w00, w01, w10, w11, total_scale);
      }
    }
  }
}

static inline void set_direct_references(const PixelPos *mb, char *l0_rFrame, char *l1_rFrame, PicMotionParams **mv_info)
{
  if (mb->available)
  {
    char *ref_idx = mv_info[mb->pos_y][mb->pos_x].ref_idx;
    *l0_rFrame  = ref_idx[LIST_0];
    *l1_rFrame  = ref_idx[LIST_1];
  }
  else
  {
    *l0_rFrame  = -1;
    *l1_rFrame  = -1;
  }
}


static void set_direct_references_mb_field(const PixelPos *mb, char *l0_rFrame, char *l1_rFrame, PicMotionParams **mv_info, Macroblock *mb_data)
{
  if (mb->available)
  {
    char *ref_idx = mv_info[mb->pos_y][mb->pos_x].ref_idx;
    if (mb_data[mb->mb_addr].mb_field)
    {
      *l0_rFrame  = ref_idx[LIST_0];
      *l1_rFrame  = ref_idx[LIST_1];
    }
    else
    {
      *l0_rFrame  = (ref_idx[LIST_0] < 0) ? ref_idx[LIST_0] : ref_idx[LIST_0] * 2;
      *l1_rFrame  = (ref_idx[LIST_1] < 0) ? ref_idx[LIST_1] : ref_idx[LIST_1] * 2;
    }
  }
  else
  {
    *l0_rFrame  = -1;
    *l1_rFrame  = -1;
  }
}

static void set_direct_references_mb_frame(const PixelPos *mb, char *l0_rFrame, char *l1_rFrame, PicMotionParams **mv_info, Macroblock *mb_data)
{
  if (mb->available)
  {
    char *ref_idx = mv_info[mb->pos_y][mb->pos_x].ref_idx;
    if (mb_data[mb->mb_addr].mb_field)
    {
      *l0_rFrame  = (ref_idx[LIST_0] >> 1);
      *l1_rFrame  = (ref_idx[LIST_1] >> 1);
    }
    else
    {
      *l0_rFrame  = ref_idx[LIST_0];
      *l1_rFrame  = ref_idx[LIST_1];
    }
  }
  else
  {
    *l0_rFrame  = -1;
    *l1_rFrame  = -1;
  }
}

void prepare_direct_params(Macroblock *currMB, StorablePicture *dec_picture, MotionVector *pmvl0, MotionVector *pmvl1, char *l0_rFrame, char *l1_rFrame)
{
  Slice *currSlice = currMB->p_Slice;
  char l0_refA, l0_refB, l0_refC;
  char l1_refA, l1_refB, l1_refC;
  PicMotionParams **mv_info = dec_picture->mv_info;
  
  PixelPos mb[4];

  get_neighbors(currMB, mb, 0, 0, 16);

  if (!currSlice->mb_aff_frame_flag)
  {
    set_direct_references(&mb[0], &l0_refA, &l1_refA, mv_info);
    set_direct_references(&mb[1], &l0_refB, &l1_refB, mv_info);
    set_direct_references(&mb[2], &l0_refC, &l1_refC, mv_info);
  }
  else
  {
    VideoParameters *p_Vid = currMB->p_Vid;
    if (currMB->mb_field)
    {
      set_direct_references_mb_field(&mb[0], &l0_refA, &l1_refA, mv_info, p_Vid->mb_data);
      set_direct_references_mb_field(&mb[1], &l0_refB, &l1_refB, mv_info, p_Vid->mb_data);
      set_direct_references_mb_field(&mb[2], &l0_refC, &l1_refC, mv_info, p_Vid->mb_data);
    }
    else
    {
      set_direct_references_mb_frame(&mb[0], &l0_refA, &l1_refA, mv_info, p_Vid->mb_data);
      set_direct_references_mb_frame(&mb[1], &l0_refB, &l1_refB, mv_info, p_Vid->mb_data);
      set_direct_references_mb_frame(&mb[2], &l0_refC, &l1_refC, mv_info, p_Vid->mb_data);
    }
  }

  *l0_rFrame = (char) imin(imin((unsigned char) l0_refA, (unsigned char) l0_refB), (unsigned char) l0_refC);
  *l1_rFrame = (char) imin(imin((unsigned char) l1_refA, (unsigned char) l1_refB), (unsigned char) l1_refC);

  if (*l0_rFrame >=0)
    currMB->GetMVPredictor (currMB, mb, pmvl0, *l0_rFrame, mv_info, LIST_0, 0, 0, 16, 16);

  if (*l1_rFrame >=0)
    currMB->GetMVPredictor (currMB, mb, pmvl1, *l1_rFrame, mv_info, LIST_1, 0, 0, 16, 16);
}

