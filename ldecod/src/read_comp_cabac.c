/*!
 ***********************************************************************
 * \file read_comp_cabac.c
 *
 * \brief
 *     Read Coefficient Components
 *
 * \author
 *    Main contributors (see contributors.h for copyright, address and affiliation details)
 *    - Alexis Michael Tourapis         <alexismt@ieee.org>
 ***********************************************************************
*/

#include "contributors.h"

#include "global.h"
#include "elements.h"
#include "macroblock.h"
#include "cabac.h"
#include "vlc.h"
#include "transform.h"

#if TRACE
#define TRACE_STRING(s) strncpy(currSE.tracestring, s, TRACESTRING_SIZE)
#define TRACE_DECBITS(i) dectracebitcnt(1)
#define TRACE_PRINTF(s) sprintf(type, "%s", s);
#define TRACE_STRING_P(s) strncpy(currSE->tracestring, s, TRACESTRING_SIZE)
#else
#define TRACE_STRING(s)
#define TRACE_DECBITS(i)
#define TRACE_PRINTF(s) 
#define TRACE_STRING_P(s)
#endif

//! look up tables for FRExt_chroma support
static const unsigned char subblk_offset_x[3][8][4] =
{
  {
    {0, 4, 0, 4},
    {0, 4, 0, 4},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}, 
  },
  { 
    {0, 4, 0, 4},
    {0, 4, 0, 4},
    {0, 4, 0, 4},
    {0, 4, 0, 4},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}, 
  },
  {
    {0, 4, 0, 4},
    {8,12, 8,12},
    {0, 4, 0, 4},
    {8,12, 8,12},
    {0, 4, 0, 4},
    {8,12, 8,12},
    {0, 4, 0, 4},
    {8,12, 8,12}  
  }
};

static const unsigned char subblk_offset_y[3][8][4] =
{
  {
    {0, 0, 4, 4},
    {0, 0, 4, 4},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  { 
    {0, 0, 4, 4},
    {8, 8,12,12},
    {0, 0, 4, 4},
    {8, 8,12,12},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  { 
    {0, 0, 4, 4},
    {0, 0, 4, 4},
    {8, 8,12,12},
    {8, 8,12,12},
    {0, 0, 4, 4},
    {0, 0, 4, 4},
    {8, 8,12,12},
    {8, 8,12,12}
  }
};

extern void  check_dp_neighbors (Macroblock *currMB);
extern void  read_delta_quant   (SyntaxElement *currSE, DataPartition *dP, Macroblock *currMB, const byte *partMap, int type);

/*!
************************************************************************
* \brief
*    Get coefficients (run/level) of 4x4 blocks in a SMB
*    from the NAL (CABAC Mode)
************************************************************************
*/
static void read_comp_coeff_4x4_smb_CABAC (Macroblock *currMB, SyntaxElement *currSE, ColorPlane pl, int block_y, int block_x, int start_scan)
{
  int i,j,k;
  int level = 1;
  DataPartition *dP;
  Slice *currSlice = currMB->p_Slice;
  const byte *partMap = assignSE2partition[currSlice->dp_mode];

  for (j = block_y; j < block_y + BLOCK_SIZE_8x8; j += 4)
  {
    currMB->subblock_y = j; // position for coeff_count ctx

    for (i = block_x; i < block_x + BLOCK_SIZE_8x8; i += 4)
    {
      currMB->subblock_x = i; // position for coeff_count ctx
      level = 1;

      if (start_scan == 0)
      {
        /*
        * make distinction between INTRA and INTER coded
        * luminance coefficients
        */
        currSE->type = (currMB->is_intra_block ? SE_LUM_DC_INTRA : SE_LUM_DC_INTER);  
        dP = &(currSlice->partArr[partMap[currSE->type]]);
        if (dP->bitstream->ei_flag)  
          currSE->mapping = linfo_levrun_inter;
        else                                                     
          currSE->reading = readRunLevel_CABAC;

#if TRACE
        if (pl == PLANE_Y)
          sprintf(currSE->tracestring, "Luma sng ");
        else if (pl == PLANE_U)
          sprintf(currSE->tracestring, "Cb   sng ");
        else
          sprintf(currSE->tracestring, "Cr   sng ");  
#endif

        dP->readSyntaxElement(currMB, currSE, dP);
        level = currSE->value1;
      }

      if (level != 0)
      {
        // make distinction between INTRA and INTER coded luminance coefficients
        currSE->type = (currMB->is_intra_block ? SE_LUM_AC_INTRA : SE_LUM_AC_INTER);  
        dP = &(currSlice->partArr[partMap[currSE->type]]);

        if (dP->bitstream->ei_flag)  
          currSE->mapping = linfo_levrun_inter;
        else                                                     
          currSE->reading = readRunLevel_CABAC;

        for(k = 1; (k < 17) && (level != 0); ++k)
        {
#if TRACE
          if (pl == PLANE_Y)
            sprintf(currSE->tracestring, "Luma sng ");
          else if (pl == PLANE_U)
            sprintf(currSE->tracestring, "Cb   sng ");
          else
            sprintf(currSE->tracestring, "Cr   sng ");  
#endif

          dP->readSyntaxElement(currMB, currSE, dP);
          level = currSE->value1;
        }
      }
    }
  }
}

/*!
************************************************************************
* \brief
*    Get coefficients (run/level) of all 4x4 blocks in a MB
*    from the NAL (CABAC Mode)
************************************************************************
*/
static void read_comp_coeff_4x4_CABAC (Macroblock *currMB, SyntaxElement *currSE, ColorPlane pl, /*int (*InvLevelScale4x4)[4], int qp_per,*/ int cbp)
{
  Slice *currSlice = currMB->p_Slice;
  VideoParameters *p_Vid = currMB->p_Vid;
  int start_scan = IS_I16MB (currMB)? 1 : 0; 
  int block_y, block_x;

  if( pl == PLANE_Y || (p_Vid->separate_colour_plane_flag != 0) )
    currSE->context = (IS_I16MB(currMB) ? LUMA_16AC: LUMA_4x4);
  else if (pl == PLANE_U)
    currSE->context = (IS_I16MB(currMB) ? CB_16AC: CB_4x4);
  else
    currSE->context = (IS_I16MB(currMB) ? CR_16AC: CR_4x4);  

  for (block_y = 0; block_y < MB_BLOCK_SIZE; block_y += BLOCK_SIZE_8x8) /* all modes */
  {
    for (block_x = 0; block_x < MB_BLOCK_SIZE; block_x += BLOCK_SIZE_8x8)
    {
      if (cbp & (1 << ((block_y >> 2) + (block_x >> 3))))  // are there any coeff in current block at all
      {
        read_comp_coeff_4x4_smb_CABAC (currMB, currSE, pl, block_y, block_x, start_scan);				
      }
    }
  }
}


/*!
************************************************************************
* \brief
*    Get coefficients (run/level) of all 4x4 blocks in a MB
*    from the NAL (CABAC Mode)
************************************************************************
*/
static void read_comp_coeff_4x4_CABAC_ls (Macroblock *currMB, SyntaxElement *currSE, ColorPlane pl,/* int (*InvLevelScale4x4)[4], int qp_per,*/ int cbp)
{
  VideoParameters *p_Vid = currMB->p_Vid;
  int start_scan = IS_I16MB (currMB)? 1 : 0; 
  int block_y, block_x;

  if( pl == PLANE_Y || (p_Vid->separate_colour_plane_flag != 0) )
    currSE->context = (IS_I16MB(currMB) ? LUMA_16AC: LUMA_4x4);
  else if (pl == PLANE_U)
    currSE->context = (IS_I16MB(currMB) ? CB_16AC: CB_4x4);
  else
    currSE->context = (IS_I16MB(currMB) ? CR_16AC: CR_4x4);  

  for (block_y = 0; block_y < MB_BLOCK_SIZE; block_y += BLOCK_SIZE_8x8) /* all modes */
  {
    for (block_x = 0; block_x < MB_BLOCK_SIZE; block_x += BLOCK_SIZE_8x8)
    {
      if (cbp & (1 << ((block_y >> 2) + (block_x >> 3))))  // are there any coeff in current block at all
      {
        read_comp_coeff_4x4_smb_CABAC (currMB, currSE, pl, block_y, block_x, start_scan);
      }
    }
  }
}


/*!
************************************************************************
* \brief
*    Get coefficients (run/level) of one 8x8 block
*    from the NAL (CABAC Mode)
************************************************************************
*/
static void readCompCoeff8x8_CABAC (Macroblock *currMB, SyntaxElement *currSE, ColorPlane pl, int b8)
{
  if (currMB->cbp & (1<<b8))  // are there any coefficients in the current block
  {
    VideoParameters *p_Vid = currMB->p_Vid;
    int k;
    int level = 1;

    DataPartition *dP;
    Slice *currSlice = currMB->p_Slice;
    const byte *partMap = assignSE2partition[currSlice->dp_mode];

    // === set offset in current macroblock ===
    if (pl==PLANE_Y || (p_Vid->separate_colour_plane_flag != 0))  
      currSE->context = LUMA_8x8;
    else if (pl==PLANE_U)
      currSE->context = CB_8x8;
    else
      currSE->context = CR_8x8;  

    currSE->reading = readRunLevel_CABAC;

    // Read DC
    currSE->type = ((currMB->is_intra_block == 1) ? SE_LUM_DC_INTRA : SE_LUM_DC_INTER ); // Intra or Inter?
    dP = &(currSlice->partArr[partMap[currSE->type]]);

#if TRACE
    if (pl==PLANE_Y)
      sprintf(currSE->tracestring, "Luma8x8 DC sng ");
    else if (pl==PLANE_U)
      sprintf(currSE->tracestring, "Cb  8x8 DC sng "); 
    else 
      sprintf(currSE->tracestring, "Cr  8x8 DC sng "); 
#endif        

    dP->readSyntaxElement(currMB, currSE, dP);
    level = currSE->value1;

    //============ decode =============
    if (level != 0)    /* leave if level == 0 */
    {
      // AC coefficients
      currSE->type    = ((currMB->is_intra_block == 1) ? SE_LUM_AC_INTRA : SE_LUM_AC_INTER);
      dP = &(currSlice->partArr[partMap[currSE->type]]);

      for(k = 1;(k < 65) && (level != 0);++k)
      {
#if TRACE
        if (pl==PLANE_Y)
          sprintf(currSE->tracestring, "Luma8x8 sng ");
        else if (pl==PLANE_U)
          sprintf(currSE->tracestring, "Cb  8x8 sng "); 
        else 
          sprintf(currSE->tracestring, "Cr  8x8 sng "); 
#endif

        dP->readSyntaxElement(currMB, currSE, dP);
        level = currSE->value1;
      }
    }        
  }
}

/*!
************************************************************************
* \brief
*    Get coefficients (run/level) of one 8x8 block
*    from the NAL (CABAC Mode - lossless)
************************************************************************
*/
static void readCompCoeff8x8_CABAC_lossless (Macroblock *currMB, SyntaxElement *currSE, ColorPlane pl, int b8)
{
  if (currMB->cbp & (1<<b8))  // are there any coefficients in the current block
  {
    VideoParameters *p_Vid = currMB->p_Vid;
    int k;
    int level = 1;

    DataPartition *dP;
    Slice *currSlice = currMB->p_Slice;
    const byte *partMap = assignSE2partition[currSlice->dp_mode];

    if (pl==PLANE_Y || (p_Vid->separate_colour_plane_flag != 0))  
      currSE->context = LUMA_8x8;
    else if (pl==PLANE_U)
      currSE->context = CB_8x8;
    else
      currSE->context = CR_8x8;  

    currSE->reading = readRunLevel_CABAC;

    for(k=0; (k < 65) && (level != 0);++k)
    {
      //============ read =============
      /*
      * make distinction between INTRA and INTER coded
      * luminance coefficients
      */
      currSE->type    = ((currMB->is_intra_block == 1)
        ? (k==0 ? SE_LUM_DC_INTRA : SE_LUM_AC_INTRA) 
        : (k==0 ? SE_LUM_DC_INTER : SE_LUM_AC_INTER));

#if TRACE
      if (pl==PLANE_Y)
        sprintf(currSE->tracestring, "Luma8x8 sng ");
      else if (pl==PLANE_U)
        sprintf(currSE->tracestring, "Cb  8x8 sng "); 
      else 
        sprintf(currSE->tracestring, "Cr  8x8 sng "); 
#endif

      dP = &(currSlice->partArr[partMap[currSE->type]]);
      currSE->reading = readRunLevel_CABAC;

      dP->readSyntaxElement(currMB, currSE, dP);
      level = currSE->value1;
    }
  }
}


/*!
************************************************************************
* \brief
*    Get coefficients (run/level) of 8x8 blocks in a MB
*    from the NAL (CABAC Mode)
************************************************************************
*/
static void read_comp_coeff_8x8_MB_CABAC (Macroblock *currMB, SyntaxElement *currSE, ColorPlane pl)
{
  //======= 8x8 transform size & CABAC ========
  readCompCoeff8x8_CABAC (currMB, currSE, pl, 0); 
  readCompCoeff8x8_CABAC (currMB, currSE, pl, 1); 
  readCompCoeff8x8_CABAC (currMB, currSE, pl, 2); 
  readCompCoeff8x8_CABAC (currMB, currSE, pl, 3); 
}


/*!
************************************************************************
* \brief
*    Get coefficients (run/level) of 8x8 blocks in a MB
*    from the NAL (CABAC Mode)
************************************************************************
*/
static void read_comp_coeff_8x8_MB_CABAC_ls (Macroblock *currMB, SyntaxElement *currSE, ColorPlane pl)
{
  //======= 8x8 transform size & CABAC ========
  readCompCoeff8x8_CABAC_lossless (currMB, currSE, pl, 0); 
  readCompCoeff8x8_CABAC_lossless (currMB, currSE, pl, 1); 
  readCompCoeff8x8_CABAC_lossless (currMB, currSE, pl, 2); 
  readCompCoeff8x8_CABAC_lossless (currMB, currSE, pl, 3); 
}


/*!
 ************************************************************************
 * \brief
 *    Get coded block pattern and coefficients (run/level)
 *    from the NAL
 ************************************************************************
 */
static void read_CBP_and_coeffs_from_NAL_CABAC_420(Macroblock *currMB)
{
  int i,j;
  int level;
  int cbp;
  SyntaxElement currSE;
  DataPartition *dP = NULL;
  Slice *currSlice = currMB->p_Slice;
  const byte *partMap = assignSE2partition[currSlice->dp_mode];

  VideoParameters *p_Vid = currMB->p_Vid;

  int intra = (currMB->is_intra_block == TRUE);  

  StorablePicture *dec_picture = currSlice->dec_picture;
  int yuv = dec_picture->chroma_format_idc - 1;

  if (!IS_I16MB (currMB))
  {
    int need_transform_size_flag;
    //=====   C B P   =====
    //---------------------
    currSE.type = (currMB->mb_type == I4MB || currMB->mb_type == SI4MB || currMB->mb_type == I8MB) 
      ? SE_CBP_INTRA
      : SE_CBP_INTER;

    dP = &(currSlice->partArr[partMap[currSE.type]]);

    if (dP->bitstream->ei_flag)
    {
      currSE.mapping = (currMB->mb_type == I4MB || currMB->mb_type == SI4MB || currMB->mb_type == I8MB)
        ? currSlice->linfo_cbp_intra
        : currSlice->linfo_cbp_inter;
    }
    else
    {
      currSE.reading = read_CBP_CABAC;
    }

    TRACE_STRING("coded_block_pattern");
    dP->readSyntaxElement(currMB, &currSE, dP);
    currMB->cbp = cbp = currSE.value1;

    //============= Transform size flag for INTER MBs =============
    //-------------------------------------------------------------
    need_transform_size_flag = (((currMB->mb_type >= 1 && currMB->mb_type <= 3)||
      (IS_DIRECT(currMB) && p_Vid->active_sps->direct_8x8_inference_flag) ||
      (currMB->NoMbPartLessThan8x8Flag))
      && currMB->mb_type != I8MB && currMB->mb_type != I4MB
      && (currMB->cbp&15)
      && currSlice->Transform8x8Mode);

    if (need_transform_size_flag)
    {
      currSE.type   =  SE_HEADER;
      dP = &(currSlice->partArr[partMap[SE_HEADER]]);
      currSE.reading = readMB_transform_size_flag_CABAC;
      TRACE_STRING("transform_size_8x8_flag");

      // read CAVLC transform_size_8x8_flag
      if (dP->bitstream->ei_flag)
      {
        currSE.len = 1;
        readSyntaxElement_FLC(&currSE, dP->bitstream);
      } 
      else
      {
        dP->readSyntaxElement(currMB, &currSE, dP);
      }
      currMB->luma_transform_size_8x8_flag = (Boolean) currSE.value1;
    }

    //=====   DQUANT   =====
    //----------------------
    // Delta quant only if nonzero coeffs
    if (cbp !=0)
    {
      read_delta_quant(&currSE, dP, currMB, partMap, ((currMB->is_intra_block == FALSE)) ? SE_DELTA_QUANT_INTER : SE_DELTA_QUANT_INTRA);
    }
  }
  else // read DC coeffs for new intra modes
  {
    cbp = currMB->cbp;
  
    read_delta_quant(&currSE, dP, currMB, partMap, SE_DELTA_QUANT_INTRA);

    if (!currMB->dpl_flag)
    {
      int k;
      currSE.type = SE_LUM_DC_INTRA;
      dP = &(currSlice->partArr[partMap[currSE.type]]);

      currSE.context      = LUMA_16DC;
      currSE.type         = SE_LUM_DC_INTRA;

      if (dP->bitstream->ei_flag)
      {
        currSE.mapping = linfo_levrun_inter;
      }
      else
      {
        currSE.reading = readRunLevel_CABAC;
      }

      level = 1;                            // just to get inside the loop

      for(k = 0; (k < 17) && (level != 0); ++k)
      {
#if TRACE
        snprintf(currSE.tracestring, TRACESTRING_SIZE, "DC luma 16x16 ");
#endif
        dP->readSyntaxElement(currMB, &currSE, dP);
        level = currSE.value1;
      }
    }
  }

  update_qp(currMB, currSlice->qp);

  // luma coefficients
  //======= Other Modes & CABAC ========
  //------------------------------------          
  if (cbp)
  {
    if(currMB->luma_transform_size_8x8_flag) 
    {
      //======= 8x8 transform size & CABAC ========
      //read_comp_coeff_8x8_MB_CABAC read_comp_coeff_8x8_MB_CABAC_ls
      currMB->read_comp_coeff_8x8_CABAC (currMB, &currSE, PLANE_Y); 
    }
    else
    {
      //InvLevelScale4x4 = intra? currSlice->InvLevelScale4x4_Intra[currSlice->colour_plane_id][qp_rem] : currSlice->InvLevelScale4x4_Inter[currSlice->colour_plane_id][qp_rem];
			//read_comp_coeff_4x4_CABAC read_comp_coeff_4x4_CABAC_ls        				
      currMB->read_comp_coeff_4x4_CABAC (currMB, &currSE, PLANE_Y, cbp);	
    }
  }
  //========================== CHROMA DC ============================
  //-----------------------------------------------------------------
  // chroma DC coeff
  if(cbp>15)
  {   
    int ll, k;

    for (ll = 0; ll < 3; ll += 2)
    {
      //===================== CHROMA DC YUV420 ======================
      memset(currSlice->cofu, 0, 4 *sizeof(int));
      //coef_ctr=-1;

      level = 1;
      currMB->is_v_block  = ll;
      currSE.context      = CHROMA_DC;
      currSE.type         = (intra ? SE_CHR_DC_INTRA : SE_CHR_DC_INTER);

      dP = &(currSlice->partArr[partMap[currSE.type]]);

      if (dP->bitstream->ei_flag)
        currSE.mapping = linfo_levrun_c2x2;
      else
        currSE.reading = readRunLevel_CABAC;

      for(k = 0; (k < (p_Vid->num_cdc_coeff + 1))&&(level!=0);++k)
      {
#if TRACE
        snprintf(currSE.tracestring, TRACESTRING_SIZE, "2x2 DC Chroma ");
#endif

        dP->readSyntaxElement(currMB, &currSE, dP);
        level = currSE.value1;
      }
    }      
  }

  //========================== CHROMA AC ============================
  //-----------------------------------------------------------------
  // chroma AC coeff, all zero fram start_scan
  if (cbp >31)
  {
    currSE.context      = CHROMA_AC;
    currSE.type         = (currMB->is_intra_block ? SE_CHR_AC_INTRA : SE_CHR_AC_INTER);

    dP = &(currSlice->partArr[partMap[currSE.type]]);

    if (dP->bitstream->ei_flag)
      currSE.mapping = linfo_levrun_inter;
    else
      currSE.reading = readRunLevel_CABAC;

    if(currMB->is_lossless == FALSE)
    {
      int b4, b8, k;
      for (b8=0; b8 < p_Vid->num_blk8x8_uv; ++b8)
      {
        for (b4 = 0; b4 < 4; ++b4)
        {
          currMB->subblock_y = subblk_offset_y[yuv][b8][b4];
          currMB->subblock_x = subblk_offset_x[yuv][b8][b4];

          level = 1;

          for(k = 0; (k < 16) && (level != 0);++k)
          {
#if TRACE
            snprintf(currSE.tracestring, TRACESTRING_SIZE, "AC Chroma ");
#endif

            dP->readSyntaxElement(currMB, &currSE, dP);
            level = currSE.value1;
          } 
        }
      }
    }
    else
    {
      int b4, b8, k;
      for (b8=0; b8 < p_Vid->num_blk8x8_uv; ++b8)
      {
        for (b4=0; b4 < 4; ++b4)
        {
          level=1;

          currMB->subblock_y = subblk_offset_y[yuv][b8][b4];
          currMB->subblock_x = subblk_offset_x[yuv][b8][b4];

          for(k=0;(k<16)&&(level!=0);++k)
          {
#if TRACE
            snprintf(currSE.tracestring, TRACESTRING_SIZE, "AC Chroma ");
#endif
            dP->readSyntaxElement(currMB, &currSE, dP);
            level = currSE.value1;
          } 
        }
      } 
    } //for (b4=0; b4 < 4; b4++)      
  }  
}


/*!
 ************************************************************************
 * \brief
 *    Get coded block pattern and coefficients (run/level)
 *    from the NAL
 ************************************************************************
 */
static void read_CBP_and_coeffs_from_NAL_CABAC_400(Macroblock *currMB)
{
  int k;
  int level;
  int cbp;
  SyntaxElement currSE;
  DataPartition *dP = NULL;
  Slice *currSlice = currMB->p_Slice;
  const byte *partMap = assignSE2partition[currSlice->dp_mode];
  VideoParameters *p_Vid = currMB->p_Vid;
  int intra = (currMB->is_intra_block == TRUE);
  int need_transform_size_flag;

  // read CBP if not new intra mode
  if (!IS_I16MB (currMB))
  {
    //=====   C B P   =====
    //---------------------
    currSE.type = (currMB->mb_type == I4MB || currMB->mb_type == SI4MB || currMB->mb_type == I8MB) 
      ? SE_CBP_INTRA
      : SE_CBP_INTER;

    dP = &(currSlice->partArr[partMap[currSE.type]]);

    if (dP->bitstream->ei_flag)
    {
      currSE.mapping = (currMB->mb_type == I4MB || currMB->mb_type == SI4MB || currMB->mb_type == I8MB)
        ? currSlice->linfo_cbp_intra
        : currSlice->linfo_cbp_inter;
    }
    else
    {
      currSE.reading = read_CBP_CABAC;
    }

    TRACE_STRING("coded_block_pattern");
    dP->readSyntaxElement(currMB, &currSE, dP);
    currMB->cbp = cbp = currSE.value1;


    //============= Transform size flag for INTER MBs =============
    //-------------------------------------------------------------
    need_transform_size_flag = (((currMB->mb_type >= 1 && currMB->mb_type <= 3)||
      (IS_DIRECT(currMB) && p_Vid->active_sps->direct_8x8_inference_flag) ||
      (currMB->NoMbPartLessThan8x8Flag))
      && currMB->mb_type != I8MB && currMB->mb_type != I4MB
      && (currMB->cbp&15)
      && currSlice->Transform8x8Mode);

    if (need_transform_size_flag)
    {
      currSE.type   =  SE_HEADER;
      dP = &(currSlice->partArr[partMap[SE_HEADER]]);
      currSE.reading = readMB_transform_size_flag_CABAC;
      TRACE_STRING("transform_size_8x8_flag");

      // read CAVLC transform_size_8x8_flag
      if (dP->bitstream->ei_flag)
      {
        currSE.len = 1;
        readSyntaxElement_FLC(&currSE, dP->bitstream);
      } 
      else
      {
        dP->readSyntaxElement(currMB, &currSE, dP);
      }
      currMB->luma_transform_size_8x8_flag = (Boolean) currSE.value1;
    }

    //=====   DQUANT   =====
    //----------------------
    // Delta quant only if nonzero coeffs
    if (cbp !=0)
    {
      read_delta_quant(&currSE, dP, currMB, partMap, ((currMB->is_intra_block == FALSE)) ? SE_DELTA_QUANT_INTER : SE_DELTA_QUANT_INTRA);
    }
  }
  else // read DC coeffs for new intra modes
  {
    cbp = currMB->cbp;  
    read_delta_quant(&currSE, dP, currMB, partMap, SE_DELTA_QUANT_INTRA);
		
    if (!currMB->dpl_flag)
    {
      currSE.type = SE_LUM_DC_INTRA;
      dP = &(currSlice->partArr[partMap[currSE.type]]);

      currSE.context      = LUMA_16DC;
      currSE.type         = SE_LUM_DC_INTRA;

      if (dP->bitstream->ei_flag)
      {
        currSE.mapping = linfo_levrun_inter;
      }
      else
      {
        currSE.reading = readRunLevel_CABAC;
      }

      level = 1;                            // just to get inside the loop

      for(k = 0; (k < 17) && (level != 0); ++k)
      {
#if TRACE
        snprintf(currSE.tracestring, TRACESTRING_SIZE, "DC luma 16x16 ");
#endif
        dP->readSyntaxElement(currMB, &currSE, dP);
        level = currSE.value1;
      }
    }
  }

  update_qp(currMB, currSlice->qp);

  //======= Other Modes & CABAC ========
  //------------------------------------          
  if (cbp)
  {
    if(currMB->luma_transform_size_8x8_flag) 
    {
      //======= 8x8 transform size & CABAC ========
      currMB->read_comp_coeff_8x8_CABAC (currMB, &currSE, PLANE_Y); 
    }
    else
    {      
      currMB->read_comp_coeff_4x4_CABAC (currMB, &currSE, PLANE_Y, cbp);        
    }
  }  
}

/*!
 ************************************************************************
 * \brief
 *    Get coded block pattern and coefficients (run/level)
 *    from the NAL
 ************************************************************************
 */
static void read_CBP_and_coeffs_from_NAL_CABAC_444(Macroblock *currMB)
{
  int i, k;
  int level;
  int cbp;
  SyntaxElement currSE;
  DataPartition *dP = NULL;
  Slice *currSlice = currMB->p_Slice;
  const byte *partMap = assignSE2partition[currSlice->dp_mode];
  VideoParameters *p_Vid = currMB->p_Vid;
  int uv; 
  int intra = (currMB->is_intra_block == TRUE);
  int need_transform_size_flag;

  // read CBP if not new intra mode
  if (!IS_I16MB (currMB))
  {
    //=====   C B P   =====
    //---------------------
    currSE.type = (currMB->mb_type == I4MB || currMB->mb_type == SI4MB || currMB->mb_type == I8MB) 
      ? SE_CBP_INTRA
      : SE_CBP_INTER;

    dP = &(currSlice->partArr[partMap[currSE.type]]);

    if (dP->bitstream->ei_flag)
    {
      currSE.mapping = (currMB->mb_type == I4MB || currMB->mb_type == SI4MB || currMB->mb_type == I8MB)
        ? currSlice->linfo_cbp_intra
        : currSlice->linfo_cbp_inter;
    }
    else
    {
      currSE.reading = read_CBP_CABAC;
    }

    TRACE_STRING("coded_block_pattern");
    dP->readSyntaxElement(currMB, &currSE, dP);
    currMB->cbp = cbp = currSE.value1;


    //============= Transform size flag for INTER MBs =============
    //-------------------------------------------------------------
    need_transform_size_flag = (((currMB->mb_type >= 1 && currMB->mb_type <= 3)||
      (IS_DIRECT(currMB) && p_Vid->active_sps->direct_8x8_inference_flag) ||
      (currMB->NoMbPartLessThan8x8Flag))
      && currMB->mb_type != I8MB && currMB->mb_type != I4MB
      && (currMB->cbp&15)
      && currSlice->Transform8x8Mode);

    if (need_transform_size_flag)
    {
      currSE.type   =  SE_HEADER;
      dP = &(currSlice->partArr[partMap[SE_HEADER]]);
      currSE.reading = readMB_transform_size_flag_CABAC;
      TRACE_STRING("transform_size_8x8_flag");

      // read CAVLC transform_size_8x8_flag
      if (dP->bitstream->ei_flag)
      {
        currSE.len = 1;
        readSyntaxElement_FLC(&currSE, dP->bitstream);
      } 
      else
      {
        dP->readSyntaxElement(currMB, &currSE, dP);
      }
      currMB->luma_transform_size_8x8_flag = (Boolean) currSE.value1;
    }

    //=====   DQUANT   =====
    //----------------------
    // Delta quant only if nonzero coeffs
    if (cbp !=0)
    {
      read_delta_quant(&currSE, dP, currMB, partMap, ((currMB->is_intra_block == FALSE)) ? SE_DELTA_QUANT_INTER : SE_DELTA_QUANT_INTRA);
    }
  }
  else // read DC coeffs for new intra modes
  {
    cbp = currMB->cbp;
  
    read_delta_quant(&currSE, dP, currMB, partMap, SE_DELTA_QUANT_INTRA);

    if (!currMB->dpl_flag)
    {
      currSE.type = SE_LUM_DC_INTRA;
      dP = &(currSlice->partArr[partMap[currSE.type]]);

      currSE.context      = LUMA_16DC;
      currSE.type         = SE_LUM_DC_INTRA;

      if (dP->bitstream->ei_flag)
      {
        currSE.mapping = linfo_levrun_inter;
      }
      else
      {
        currSE.reading = readRunLevel_CABAC;
      }

      level = 1;                            // just to get inside the loop

      for(k = 0; (k < 17) && (level != 0); ++k)
      {
#if TRACE
        snprintf(currSE.tracestring, TRACESTRING_SIZE, "DC luma 16x16 ");
#endif
        dP->readSyntaxElement(currMB, &currSE, dP);
        level = currSE.value1;
      }
    }
  }

  update_qp(currMB, currSlice->qp);

  //======= Other Modes & CABAC ========
  //------------------------------------          
  if (cbp)
  {
    if(currMB->luma_transform_size_8x8_flag) 
    {
      //======= 8x8 transform size & CABAC ========
      currMB->read_comp_coeff_8x8_CABAC (currMB, &currSE, PLANE_Y); 
    }
    else
    {
      //currMB->read_comp_coeff_4x4_CABAC (currMB, &currSE, PLANE_Y, InvLevelScale4x4, qp_per, cbp);        
      currMB->read_comp_coeff_4x4_CABAC (currMB, &currSE, PLANE_Y, cbp);        
    }
  }

  for (uv = 0; uv < 2; ++uv )
  {
    /*----------------------16x16DC Luma_Add----------------------*/
    if (IS_I16MB (currMB)) // read DC coeffs for new intra modes       
    {
      {              
        currSE.type = SE_LUM_DC_INTRA;
        dP = &(currSlice->partArr[partMap[currSE.type]]);

        if( (p_Vid->separate_colour_plane_flag != 0) )
          currSE.context = LUMA_16DC; 
        else
          currSE.context = (uv==0) ? CB_16DC : CR_16DC;

        if (dP->bitstream->ei_flag)
        {
          currSE.mapping = linfo_levrun_inter;
        }
        else
        {
          currSE.reading = readRunLevel_CABAC;
        }

        level = 1;                            // just to get inside the loop

        for(k=0;(k<17) && (level!=0);++k)
        {
#if TRACE
          if (uv == 0)
            snprintf(currSE.tracestring, TRACESTRING_SIZE, "DC Cb   16x16 "); 
          else
            snprintf(currSE.tracestring, TRACESTRING_SIZE, "DC Cr   16x16 ");
#endif

          dP->readSyntaxElement(currMB, &currSE, dP);
          level = currSE.value1;
        } //k loop
      } // else CAVLC
    } //IS_I16MB

    update_qp(currMB, currSlice->qp);
    if (cbp)
    {
      if(currMB->luma_transform_size_8x8_flag) 
      {
        //======= 8x8 transform size & CABAC ========
        currMB->read_comp_coeff_8x8_CABAC (currMB, &currSE, (ColorPlane) (PLANE_U + uv)); 
      }
      else //4x4
      {        
        //currMB->read_comp_coeff_4x4_CABAC (currMB, &currSE, (ColorPlane) (PLANE_U + uv), InvLevelScale4x4,  qp_per_uv[uv], cbp);
        currMB->read_comp_coeff_4x4_CABAC (currMB, &currSE, (ColorPlane) (PLANE_U + uv), cbp);
      }
    }
  } 
}

/*!
 ************************************************************************
 * \brief
 *    Get coded block pattern and coefficients (run/level)
 *    from the NAL
 ************************************************************************
 */
static void read_CBP_and_coeffs_from_NAL_CABAC_422(Macroblock *currMB)
{
  int i,j,k;
  int level;
  int cbp;
  SyntaxElement currSE;
  DataPartition *dP = NULL;
  Slice *currSlice = currMB->p_Slice;
  const byte *partMap = assignSE2partition[currSlice->dp_mode];
  int b8, b4;
  int ll;
  VideoParameters *p_Vid = currMB->p_Vid;
  int uv; 
  int intra = (currMB->is_intra_block == TRUE);
  StorablePicture *dec_picture = currSlice->dec_picture;
  int yuv = dec_picture->chroma_format_idc - 1;

  int need_transform_size_flag;

  // read CBP if not new intra mode
  if (!IS_I16MB (currMB))
  {
    //=====   C B P   =====
    //---------------------
    currSE.type = (currMB->mb_type == I4MB || currMB->mb_type == SI4MB || currMB->mb_type == I8MB) 
      ? SE_CBP_INTRA
      : SE_CBP_INTER;

    dP = &(currSlice->partArr[partMap[currSE.type]]);

    if (dP->bitstream->ei_flag)
    {
      currSE.mapping = (currMB->mb_type == I4MB || currMB->mb_type == SI4MB || currMB->mb_type == I8MB)
        ? currSlice->linfo_cbp_intra
        : currSlice->linfo_cbp_inter;
    }
    else
    {
      currSE.reading = read_CBP_CABAC;
    }

    TRACE_STRING("coded_block_pattern");
    dP->readSyntaxElement(currMB, &currSE, dP);
    currMB->cbp = cbp = currSE.value1;


    //============= Transform size flag for INTER MBs =============
    //-------------------------------------------------------------
    need_transform_size_flag = (((currMB->mb_type >= 1 && currMB->mb_type <= 3)||
      (IS_DIRECT(currMB) && p_Vid->active_sps->direct_8x8_inference_flag) ||
      (currMB->NoMbPartLessThan8x8Flag))
      && currMB->mb_type != I8MB && currMB->mb_type != I4MB
      && (currMB->cbp&15)
      && currSlice->Transform8x8Mode);

    if (need_transform_size_flag)
    {
      currSE.type   =  SE_HEADER;
      dP = &(currSlice->partArr[partMap[SE_HEADER]]);
      currSE.reading = readMB_transform_size_flag_CABAC;
      TRACE_STRING("transform_size_8x8_flag");

      // read CAVLC transform_size_8x8_flag
      if (dP->bitstream->ei_flag)
      {
        currSE.len = 1;
        readSyntaxElement_FLC(&currSE, dP->bitstream);
      } 
      else
      {
        dP->readSyntaxElement(currMB, &currSE, dP);
      }
      currMB->luma_transform_size_8x8_flag = (Boolean) currSE.value1;
    }

    //=====   DQUANT   =====
    //----------------------
    // Delta quant only if nonzero coeffs
    if (cbp !=0)
    {
      read_delta_quant(&currSE, dP, currMB, partMap, ((currMB->is_intra_block == FALSE)) ? SE_DELTA_QUANT_INTER : SE_DELTA_QUANT_INTRA);
    }
  }
  else // read DC coeffs for new intra modes
  {
    cbp = currMB->cbp;
  
    read_delta_quant(&currSE, dP, currMB, partMap, SE_DELTA_QUANT_INTRA);
		
    if (!currMB->dpl_flag)
    {
        currSE.type = SE_LUM_DC_INTRA;
        dP = &(currSlice->partArr[partMap[currSE.type]]);

        currSE.context      = LUMA_16DC;
        currSE.type         = SE_LUM_DC_INTRA;

        if (dP->bitstream->ei_flag)
        {
          currSE.mapping = linfo_levrun_inter;
        }
        else
        {
          currSE.reading = readRunLevel_CABAC;
        }

        level = 1;                            // just to get inside the loop

        for(k = 0; (k < 17) && (level != 0); ++k)
        {
#if TRACE
          snprintf(currSE.tracestring, TRACESTRING_SIZE, "DC luma 16x16 ");
#endif
          dP->readSyntaxElement(currMB, &currSE, dP);
          level = currSE.value1;
        }
    }
  }

  update_qp(currMB, currSlice->qp);

  //======= Other Modes & CABAC ========
  //------------------------------------          
  if (cbp)
  {
    if(currMB->luma_transform_size_8x8_flag) 
    {
      //======= 8x8 transform size & CABAC ========
      currMB->read_comp_coeff_8x8_CABAC (currMB, &currSE, PLANE_Y); 
    }
    else
    {
      //currMB->read_comp_coeff_4x4_CABAC (currMB, &currSE, PLANE_Y, InvLevelScale4x4, qp_per, cbp);        
      currMB->read_comp_coeff_4x4_CABAC (currMB, &currSE, PLANE_Y, cbp);        
    }
  }

  //========================== CHROMA DC ============================
  //-----------------------------------------------------------------
  // chroma DC coeff
  if(cbp>15)
  {      
    for (ll=0;ll<3;ll+=2)
    {
      uv = ll>>1;
      {
        //===================== CHROMA DC YUV422 ======================
        level=1;
        for(k=0;(k<9)&&(level!=0);++k)
        {
          currSE.context      = CHROMA_DC_2x4;
          currSE.type         = ((currMB->is_intra_block == TRUE) ? SE_CHR_DC_INTRA : SE_CHR_DC_INTER);
          currMB->is_v_block     = ll;

#if TRACE
          snprintf(currSE.tracestring, TRACESTRING_SIZE, "2x4 DC Chroma ");
#endif
          dP = &(currSlice->partArr[partMap[currSE.type]]);

          if (dP->bitstream->ei_flag)
            currSE.mapping = linfo_levrun_c2x2;
          else
            currSE.reading = readRunLevel_CABAC;

          dP->readSyntaxElement(currMB, &currSE, dP);

          level = currSE.value1;
        }
      }
    }//for (ll=0;ll<3;ll+=2)      
  }

  //========================== CHROMA AC ============================
  //-----------------------------------------------------------------
  // chroma AC coeff, all zero fram start_scan
  if (cbp<=31)
  {
  }
  else
  {
      currSE.context      = CHROMA_AC;
      currSE.type         = (currMB->is_intra_block ? SE_CHR_AC_INTRA : SE_CHR_AC_INTER);

      dP = &(currSlice->partArr[partMap[currSE.type]]);

      if (dP->bitstream->ei_flag)
        currSE.mapping = linfo_levrun_inter;
      else
        currSE.reading = readRunLevel_CABAC;

      if(currMB->is_lossless == FALSE)
      {          
        for (b8=0; b8 < p_Vid->num_blk8x8_uv; ++b8)
        {
          currMB->is_v_block = uv = (b8 > ((p_Vid->num_uv_blocks) - 1 ));

          for (b4 = 0; b4 < 4; ++b4)
          {
            currMB->subblock_y = subblk_offset_y[yuv][b8][b4];
            currMB->subblock_x = subblk_offset_x[yuv][b8][b4];

            level=1;

            for(k = 0; (k < 16) && (level != 0);++k)
            {
#if TRACE
              snprintf(currSE.tracestring, TRACESTRING_SIZE, "AC Chroma ");
#endif

              dP->readSyntaxElement(currMB, &currSE, dP);
              level = currSE.value1;
            } //for(k=0;(k<16)&&(level!=0);++k)
          }
        }
      }
      else
      {
        for (b8=0; b8 < p_Vid->num_blk8x8_uv; ++b8)
        {
          currMB->is_v_block = uv = (b8 > ((p_Vid->num_uv_blocks) - 1 ));

          for (b4=0; b4 < 4; ++b4)
          {
            level=1;

            currMB->subblock_y = subblk_offset_y[yuv][b8][b4];
            currMB->subblock_x = subblk_offset_x[yuv][b8][b4];

            for(k=0;(k<16)&&(level!=0);++k)
            {
#if TRACE
              snprintf(currSE.tracestring, TRACESTRING_SIZE, "AC Chroma ");
#endif
              dP->readSyntaxElement(currMB, &currSE, dP);
              level = currSE.value1;
            } 
          }
        } 
      } //for (b4=0; b4 < 4; b4++)
  } //if (dec_picture->chroma_format_idc != YUV400)  
}

void set_read_CBP_and_coeffs_cabac(Slice *currSlice)
{
  switch (currSlice->p_Vid->active_sps->chroma_format_idc)
  {
  case YUV444:
    if (currSlice->p_Vid->separate_colour_plane_flag == 0)
    {
      currSlice->read_CBP_and_coeffs_from_NAL = read_CBP_and_coeffs_from_NAL_CABAC_444;
    }
    else
    {
      currSlice->read_CBP_and_coeffs_from_NAL = read_CBP_and_coeffs_from_NAL_CABAC_400;
    }
    break;
  case YUV422:
    currSlice->read_CBP_and_coeffs_from_NAL = read_CBP_and_coeffs_from_NAL_CABAC_422;
    break;
  case YUV420:
    currSlice->read_CBP_and_coeffs_from_NAL = read_CBP_and_coeffs_from_NAL_CABAC_420;
    break;
  case YUV400:
    currSlice->read_CBP_and_coeffs_from_NAL = read_CBP_and_coeffs_from_NAL_CABAC_400;
    break;
  default:
    assert (1);
    currSlice->read_CBP_and_coeffs_from_NAL = NULL;
    break;
  }
}


/*!
************************************************************************
* \brief
*    setup coefficient reading functions for CABAC
*
************************************************************************
*/
void set_read_comp_coeff_cabac(Macroblock *currMB)
{
  if (currMB->is_lossless == FALSE)
  {
    currMB->read_comp_coeff_4x4_CABAC = read_comp_coeff_4x4_CABAC;
    currMB->read_comp_coeff_8x8_CABAC = read_comp_coeff_8x8_MB_CABAC;
  }
  else
  {
    currMB->read_comp_coeff_4x4_CABAC = read_comp_coeff_4x4_CABAC_ls;
    currMB->read_comp_coeff_8x8_CABAC = read_comp_coeff_8x8_MB_CABAC_ls;
  }
}

