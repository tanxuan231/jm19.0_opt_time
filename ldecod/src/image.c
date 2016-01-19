
/*!
 ***********************************************************************
 * \file image.c
 *
 * \brief
 *    Decode a Slice
 *
 * \author
 *    Main contributors (see contributors.h for copyright, address and affiliation details)
 *    - Inge Lille-Langoy               <inge.lille-langoy@telenor.com>
 *    - Rickard Sjoberg                 <rickard.sjoberg@era.ericsson.se>
 *    - Jani Lainema                    <jani.lainema@nokia.com>
 *    - Sebastian Purreiter             <sebastian.purreiter@mch.siemens.de>
 *    - Byeong-Moon Jeon                <jeonbm@lge.com>
 *    - Thomas Wedi                     <wedi@tnt.uni-hannover.de>
 *    - Gabi Blaettermann
 *    - Ye-Kui Wang                     <wyk@ieee.org>
 *    - Antti Hallapuro                 <antti.hallapuro@nokia.com>
 *    - Alexis Tourapis                 <alexismt@ieee.org>
 *    - Jill Boyce                      <jill.boyce@thomson.net>
 *    - Saurav K Bandyopadhyay          <saurav@ieee.org>
 *    - Zhenyu Wu                       <Zhenyu.Wu@thomson.net
 *    - Purvin Pandit                   <Purvin.Pandit@thomson.net>
 *
 ***********************************************************************
 */

#include "contributors.h"

#include <math.h>
#include <limits.h>

#include "global.h"
#include "image.h"
#include "fmo.h"
#include "annexb.h"
#include "nalu.h"
#include "parset.h"
#include "header.h"

#include "sei.h"
#include "mb_access.h"
#include "memalloc.h"
#include "macroblock.h"

#include "biaridecod.h"
#include "context_ini.h"
#include "cabac.h"
#include "vlc.h"
#include "fast_memory.h"

extern int testEndian(void);
void reorder_lists(Slice *currSlice);

static inline void reset_mbs(Macroblock *currMB)
{
  currMB->slice_nr = -1; 
  currMB->ei_flag  =  1;
  currMB->dpl_flag =  0;
}

static void setup_buffers(VideoParameters *p_Vid, int layer_id)
{
  CodingParameters *cps = p_Vid->p_EncodePar[layer_id];
  int i;

  if(p_Vid->last_dec_layer_id != layer_id)
  {
    //p_Vid->imgY_ref = cps->imgY_ref;
    //p_Vid->imgUV_ref = cps->imgUV_ref;
    if(cps->separate_colour_plane_flag)
    {
     for( i=0; i<MAX_PLANE; i++ )
     {
       p_Vid->mb_data_JV[i] = cps->mb_data_JV[i];
       p_Vid->intra_block_JV[i] = cps->intra_block_JV[i];
       p_Vid->ipredmode_JV[i] = cps->ipredmode_JV[i];
       p_Vid->siblock_JV[i] = cps->siblock_JV[i];
     }
     p_Vid->mb_data = NULL;
     p_Vid->intra_block = NULL;
     p_Vid->ipredmode = NULL;
     p_Vid->siblock = NULL;
    }
    else
    {
      p_Vid->mb_data = cps->mb_data;
      p_Vid->intra_block = cps->intra_block;
      p_Vid->ipredmode = cps->ipredmode;
      p_Vid->siblock = cps->siblock;
    }
    p_Vid->PicPos = cps->PicPos;
    p_Vid->nz_coeff = cps->nz_coeff;
    //p_Vid->qp_per_matrix = cps->qp_per_matrix;
    //p_Vid->qp_rem_matrix = cps->qp_rem_matrix;
    p_Vid->oldFrameSizeInMbs = cps->oldFrameSizeInMbs;
    p_Vid->img2buf = cps->img2buf;
    p_Vid->last_dec_layer_id = layer_id;
  }
}


/*!
 ************************************************************************
 * \brief
 *    Initializes the parameters for a new picture
 ************************************************************************
 */
static void init_picture(VideoParameters *p_Vid, Slice *currSlice, InputParameters *p_Inp)
{
  int i;
  int nplane;
  StorablePicture *dec_picture = NULL;
  seq_parameter_set_rbsp_t *active_sps = p_Vid->active_sps;
  //DecodedPictureBuffer *p_Dpb = currSlice->p_Dpb;

  p_Vid->PicHeightInMbs = p_Vid->FrameHeightInMbs / ( 1 + currSlice->field_pic_flag );
  p_Vid->PicSizeInMbs   = p_Vid->PicWidthInMbs * p_Vid->PicHeightInMbs;
  p_Vid->FrameSizeInMbs = p_Vid->PicWidthInMbs * p_Vid->FrameHeightInMbs;

  p_Vid->bFrameInit = 1;
  if (p_Vid->dec_picture) // && p_Vid->num_dec_mb == p_Vid->PicSizeInMbs)
  {
    // this may only happen on slice loss
    exit_picture(p_Vid, &p_Vid->dec_picture);
  }
  p_Vid->dpb_layer_id = currSlice->layer_id;
  //set buffers;
  setup_buffers(p_Vid, currSlice->layer_id);

  if(currSlice->nal_reference_idc)
  {
    p_Vid->pre_frame_num = currSlice->frame_num;
  }

  if (currSlice->structure==FRAME ||currSlice->structure==TOP_FIELD)
  {
    gettime (&(p_Vid->start_time));             // start time
  }

  dec_picture = p_Vid->dec_picture = alloc_storable_picture (p_Vid, currSlice->structure, p_Vid->width, p_Vid->height, p_Vid->width_cr, p_Vid->height_cr, 1);
  dec_picture->qp = currSlice->qp;
  //dec_picture->slice_qp_delta = currSlice->slice_qp_delta;
  dec_picture->chroma_qp_offset[0] = p_Vid->active_pps->chroma_qp_index_offset;
  dec_picture->chroma_qp_offset[1] = p_Vid->active_pps->second_chroma_qp_index_offset;
  //dec_picture->iCodingType = currSlice->structure==FRAME? (currSlice->mb_aff_frame_flag? FRAME_MB_PAIR_CODING:FRAME_CODING): FIELD_CODING; //currSlice->slice_type;
  dec_picture->layer_id = currSlice->layer_id;
#if (MVC_EXTENSION_ENABLE)
  dec_picture->view_id         = currSlice->view_id;
  dec_picture->inter_view_flag = currSlice->inter_view_flag;
  dec_picture->anchor_pic_flag = currSlice->anchor_pic_flag;
#endif

  // reset all variables of the error concealment instance before decoding of every frame.
  // here the third parameter should, if perfectly, be equal to the number of slices per frame.
  // using little value is ok, the code will allocate more memory if the slice number is larger

  if (p_Vid->type > SI_SLICE)
  {
    //set_ec_flag(p_Vid, SE_PTYPE);
    p_Vid->type = P_SLICE;  // concealed element
  }

  // CAVLC init
  if (p_Vid->active_pps->entropy_coding_mode_flag == (Boolean) CAVLC)
  {
    memset(p_Vid->nz_coeff[0][0][0], -1, p_Vid->PicSizeInMbs * 48 *sizeof(byte)); // 3 * 4 * 4
  }

  // Set the slice_nr member of each MB to -1, to ensure correct when packet loss occurs
  // TO set Macroblock Map (mark all MBs as 'have to be concealed')
  if( (p_Vid->separate_colour_plane_flag != 0) )
  {
    for( nplane=0; nplane<MAX_PLANE; ++nplane )
    {      
      Macroblock *currMB = p_Vid->mb_data_JV[nplane];
      char *intra_block = p_Vid->intra_block_JV[nplane];
      for(i=0; i<(int)p_Vid->PicSizeInMbs; ++i)
      {
        reset_mbs(currMB++);
      }
      fast_memset(p_Vid->ipredmode_JV[nplane][0], DC_PRED, 16 * p_Vid->FrameHeightInMbs * p_Vid->PicWidthInMbs * sizeof(char));
      if(p_Vid->active_pps->constrained_intra_pred_flag)
      {
        for (i=0; i<(int)p_Vid->PicSizeInMbs; ++i)
        {
          intra_block[i] = 1;
        }
      }
    }
  }
  else
  {
    if(p_Vid->active_pps->constrained_intra_pred_flag)
    {
      for (i=0; i<(int)p_Vid->PicSizeInMbs; ++i)
      {
        p_Vid->intra_block[i] = 1;
      }
    }
    fast_memset(p_Vid->ipredmode[0], DC_PRED, 16 * p_Vid->FrameHeightInMbs * p_Vid->PicWidthInMbs * sizeof(char));
  }  

  dec_picture->slice_type = p_Vid->type;
  dec_picture->used_for_reference = (currSlice->nal_reference_idc != 0);
  dec_picture->idr_flag = currSlice->idr_flag;
  //dec_picture->no_output_of_prior_pics_flag = currSlice->no_output_of_prior_pics_flag;
  //dec_picture->long_term_reference_flag     = currSlice->long_term_reference_flag;
  //dec_picture->adaptive_ref_pic_buffering_flag = currSlice->adaptive_ref_pic_buffering_flag;

  dec_picture->dec_ref_pic_marking_buffer = currSlice->dec_ref_pic_marking_buffer;
  currSlice->dec_ref_pic_marking_buffer   = NULL;

  dec_picture->mb_aff_frame_flag = currSlice->mb_aff_frame_flag;
  dec_picture->PicWidthInMbs     = p_Vid->PicWidthInMbs;

  p_Vid->get_mb_block_pos = dec_picture->mb_aff_frame_flag ? get_mb_block_pos_mbaff : get_mb_block_pos_normal;
  p_Vid->getNeighbour     = dec_picture->mb_aff_frame_flag ? getAffNeighbour : getNonAffNeighbour;

  dec_picture->pic_num   = currSlice->frame_num;
  dec_picture->frame_num = currSlice->frame_num;

  //dec_picture->recovery_frame = (unsigned int) ((int) currSlice->frame_num == p_Vid->recovery_frame_num);

  dec_picture->coded_frame = (currSlice->structure==FRAME);

  dec_picture->chroma_format_idc = active_sps->chroma_format_idc;

  dec_picture->frame_mbs_only_flag = active_sps->frame_mbs_only_flag;
  dec_picture->frame_cropping_flag = active_sps->frame_cropping_flag;

  if (dec_picture->frame_cropping_flag)
  {
    dec_picture->frame_crop_left_offset   = active_sps->frame_crop_left_offset;
    dec_picture->frame_crop_right_offset  = active_sps->frame_crop_right_offset;
    dec_picture->frame_crop_top_offset    = active_sps->frame_crop_top_offset;
    dec_picture->frame_crop_bottom_offset = active_sps->frame_crop_bottom_offset;
  }
}

static void update_mbaff_macroblock_data(imgpel **cur_img, imgpel (*temp)[16], int x0, int width, int height)
{
  imgpel (*temp_evn)[16] = temp;
  imgpel (*temp_odd)[16] = temp + height; 
  imgpel **temp_img = cur_img;
  int y;

  for (y = 0; y < 2 * height; ++y)
    memcpy(*temp++, (*temp_img++ + x0), width * sizeof(imgpel));

  for (y = 0; y < height; ++y)
  {
    memcpy((*cur_img++ + x0), *temp_evn++, width * sizeof(imgpel));
    memcpy((*cur_img++ + x0), *temp_odd++, width * sizeof(imgpel));
  }
}

static void init_picture_decoding(VideoParameters *p_Vid)
{
  Slice *pSlice = p_Vid->ppSliceList[0];
  int j, i; //iDeblockMode=1;

  if(p_Vid->iSliceNumOfCurrPic >= MAX_NUM_SLICES)
  {
    error ("Maximum number of supported slices exceeded. \nPlease recompile with increased value for MAX_NUM_SLICES", 200);
  }

  if(p_Vid->pNextPPS->Valid && (int) p_Vid->pNextPPS->pic_parameter_set_id == pSlice->pic_parameter_set_id)
  {
    pic_parameter_set_rbsp_t tmpPPS;
    memcpy(&tmpPPS, &(p_Vid->PicParSet[pSlice->pic_parameter_set_id]), sizeof (pic_parameter_set_rbsp_t));
    (p_Vid->PicParSet[pSlice->pic_parameter_set_id]).slice_group_id = NULL;
    MakePPSavailable (p_Vid, p_Vid->pNextPPS->pic_parameter_set_id, p_Vid->pNextPPS);
    memcpy(p_Vid->pNextPPS, &tmpPPS, sizeof (pic_parameter_set_rbsp_t));
    tmpPPS.slice_group_id = NULL;
  }

  UseParameterSet (pSlice);
  if(pSlice->idr_flag)
    p_Vid->number=0;

  p_Vid->PicHeightInMbs = p_Vid->FrameHeightInMbs / ( 1 + pSlice->field_pic_flag );
  p_Vid->PicSizeInMbs   = p_Vid->PicWidthInMbs * p_Vid->PicHeightInMbs;
  p_Vid->FrameSizeInMbs = p_Vid->PicWidthInMbs * p_Vid->FrameHeightInMbs;
  p_Vid->structure = pSlice->structure;

  fmo_init (p_Vid, pSlice);

#if (MVC_EXTENSION_ENABLE)
  if((pSlice->layer_id>0) && (pSlice->svc_extension_flag == 0 && pSlice->NaluHeaderMVCExt.non_idr_flag == 0))
  {
   //idr_memory_management(p_Vid->p_Dpb_layer[pSlice->layer_id], p_Vid->dec_picture);
  }
  //update_ref_list(p_Vid->p_Dpb_layer[pSlice->view_id]);
  //update_ltref_list(p_Vid->p_Dpb_layer[pSlice->view_id]);
  //update_pic_num(pSlice);
  i = pSlice->view_id;
#else
  //update_pic_num(pSlice);
  i = 0;
#endif
  //init_Deblock(p_Vid, pSlice->mb_aff_frame_flag);
  //init mb_data;
  //for(j=0; j<p_Vid->iSliceNumOfCurrPic; j++)
  //{
    //if(p_Vid->ppSliceList[j]->DFDisableIdc != 1)
      //iDeblockMode=0;
#if (MVC_EXTENSION_ENABLE)
    //assert(p_Vid->ppSliceList[j]->view_id == i);
#endif
  //}
  //p_Vid->iDeblockMode = iDeblockMode;
}

void init_slice(VideoParameters *p_Vid, Slice *currSlice)
{
  int i;

  p_Vid->active_sps = currSlice->active_sps;
  p_Vid->active_pps = currSlice->active_pps;

  //currSlice->init_lists (currSlice);

#if (MVC_EXTENSION_ENABLE)
  //if (currSlice->svc_extension_flag == 0 || currSlice->svc_extension_flag == 1)
    //reorder_lists_mvc (currSlice, currSlice->ThisPOC);
  //else
    //reorder_lists (currSlice);

  if (currSlice->fs_listinterview0)
  {
    free(currSlice->fs_listinterview0);
    currSlice->fs_listinterview0 = NULL;
  }
  if (currSlice->fs_listinterview1)
  {
    free(currSlice->fs_listinterview1);
    currSlice->fs_listinterview1 = NULL;
  }
#else
  //reorder_lists (currSlice);
#endif

  if (currSlice->structure==FRAME)
  {
    //init_mbaff_lists(p_Vid, currSlice);
  }
  //p_Vid->recovery_point = 0;

  // update reference flags and set current p_Vid->ref_flag
  //if(!(currSlice->redundant_pic_cnt != 0 && p_Vid->previous_frame_num == currSlice->frame_num))
  {
    //for(i=16;i>0;i--)
    {
      //currSlice->ref_flag[i] = currSlice->ref_flag[i-1];
    }
  }
  //currSlice->ref_flag[0] = currSlice->redundant_pic_cnt==0 ? p_Vid->Is_primary_correct : p_Vid->Is_redundant_correct;
  //p_Vid->previous_frame_num = currSlice->frame_num; //p_Vid->frame_num;

  if((currSlice->active_sps->chroma_format_idc==0)||(currSlice->active_sps->chroma_format_idc==3))
  {
    currSlice->linfo_cbp_intra = linfo_cbp_intra_other;
    currSlice->linfo_cbp_inter = linfo_cbp_inter_other;
  }
  else
  {
    currSlice->linfo_cbp_intra = linfo_cbp_intra_normal;
    currSlice->linfo_cbp_inter = linfo_cbp_inter_normal;
  }
}

void decode_slice(Slice *currSlice, int current_header)
{
	if (currSlice->slice_type == I_SLICE || currSlice->slice_type == SI_SLICE)
		return;
	
  if (currSlice->active_pps->entropy_coding_mode_flag)
  {
    init_contexts  (currSlice);
    cabac_new_slice(currSlice);
  }

  //if ( (currSlice->active_pps->weighted_bipred_idc > 0  && (currSlice->slice_type == B_SLICE)) || (currSlice->active_pps->weighted_pred_flag && currSlice->slice_type !=I_SLICE))
    //fill_wp_params(currSlice);

  //printf("frame picture %d %d %d\n",currSlice->structure,currSlice->ThisPOC,currSlice->direct_spatial_mv_pred_flag);

  // decode main slice information
  if ((current_header == SOP || current_header == SOS) && currSlice->ei_flag == 0)
    decode_one_slice(currSlice);

  // setMB-Nr in case this slice was lost
  // if(currSlice->ei_flag)
  //   p_Vid->current_mb_nr = currSlice->last_mb_nr + 1;

}

static void CopyPOC(Slice *pSlice0, Slice *currSlice)
{
  //currSlice->framepoc  = pSlice0->framepoc;
  //currSlice->toppoc    = pSlice0->toppoc;
  //currSlice->bottompoc = pSlice0->bottompoc;  
  //currSlice->ThisPOC   = pSlice0->ThisPOC;
}



/*!
 ***********************************************************************
 * \brief
 *    decodes one I- or P-frame
 *
 ***********************************************************************
 */
int decode_one_frame(DecoderParams *pDecoder)
{
  VideoParameters *p_Vid = pDecoder->p_Vid;
  InputParameters *p_Inp = p_Vid->p_Inp;
  int current_header, iRet;
  Slice *currSlice; // = p_Vid->currentSlice;
  Slice **ppSliceList = p_Vid->ppSliceList;
  int iSliceNo;
  
  //read one picture first;
  p_Vid->iSliceNumOfCurrPic=0;
  current_header=0;
  p_Vid->iNumOfSlicesDecoded=0;
  p_Vid->num_dec_mb = 0;
  if(p_Vid->newframe)
  {
    if(p_Vid->pNextPPS->Valid) 
    {
      //assert((int) p_Vid->pNextPPS->pic_parameter_set_id == p_Vid->pNextSlice->pic_parameter_set_id);
      MakePPSavailable (p_Vid, p_Vid->pNextPPS->pic_parameter_set_id, p_Vid->pNextPPS);
      p_Vid->pNextPPS->Valid=0;
    }

    //get the first slice from currentslice;
    assert(ppSliceList[p_Vid->iSliceNumOfCurrPic]);
    currSlice = ppSliceList[p_Vid->iSliceNumOfCurrPic];
    ppSliceList[p_Vid->iSliceNumOfCurrPic] = p_Vid->pNextSlice;
    p_Vid->pNextSlice = currSlice;
    assert(ppSliceList[p_Vid->iSliceNumOfCurrPic]->current_slice_nr == 0);
    
    currSlice = ppSliceList[p_Vid->iSliceNumOfCurrPic];

    UseParameterSet (currSlice);

    init_picture(p_Vid, currSlice, p_Inp);
    
    p_Vid->iSliceNumOfCurrPic++;
    current_header = SOS;
  }
  while(current_header != SOP && current_header !=EOS)
  {
    //no pending slices;
    assert(p_Vid->iSliceNumOfCurrPic < p_Vid->iNumOfSlicesAllocated);
    if(!ppSliceList[p_Vid->iSliceNumOfCurrPic])
    {
      ppSliceList[p_Vid->iSliceNumOfCurrPic] = malloc_slice(p_Inp, p_Vid);
    }
    currSlice = ppSliceList[p_Vid->iSliceNumOfCurrPic];

    //p_Vid->currentSlice = currSlice;
    currSlice->p_Vid = p_Vid;
    currSlice->p_Inp = p_Inp;
    //currSlice->p_Dpb = p_Vid->p_Dpb_layer[0]; //set default value;
    currSlice->next_header = -8888;
    currSlice->num_dec_mb = 0;
    currSlice->coeff_ctr = -1;
    currSlice->pos       =  0;
    //currSlice->is_reset_coeff = FALSE;
    //currSlice->is_reset_coeff_cr = FALSE;

    current_header = read_new_slice(currSlice);
    //init;
    currSlice->current_header = current_header;

    // error tracking of primary and redundant slices.
    //Error_tracking(p_Vid, currSlice);
    // If primary and redundant are received and primary is correct, discard the redundant
    // else, primary slice will be replaced with redundant slice.
    if(currSlice->frame_num == p_Vid->previous_frame_num && currSlice->redundant_pic_cnt !=0
      /*&& p_Vid->Is_primary_correct !=0*/ && current_header != EOS)
    {
      continue;
    }

    if((current_header != SOP && current_header !=EOS) || (p_Vid->iSliceNumOfCurrPic==0 && current_header == SOP))
    {
       currSlice->current_slice_nr = (short) p_Vid->iSliceNumOfCurrPic;
       p_Vid->dec_picture->max_slice_id = (short) imax(currSlice->current_slice_nr, p_Vid->dec_picture->max_slice_id);
       if(p_Vid->iSliceNumOfCurrPic >0)
       {
         CopyPOC(*ppSliceList, currSlice);
         ppSliceList[p_Vid->iSliceNumOfCurrPic-1]->end_mb_nr_plus1 = currSlice->start_mb_nr;
       }
       p_Vid->iSliceNumOfCurrPic++;
       if(p_Vid->iSliceNumOfCurrPic >= p_Vid->iNumOfSlicesAllocated)
       {
         Slice **tmpSliceList = (Slice **)realloc(p_Vid->ppSliceList, (p_Vid->iNumOfSlicesAllocated+MAX_NUM_DECSLICES)*sizeof(Slice*));
         if(!tmpSliceList)
         {
           tmpSliceList = calloc((p_Vid->iNumOfSlicesAllocated+MAX_NUM_DECSLICES), sizeof(Slice*));
           memcpy(tmpSliceList, p_Vid->ppSliceList, p_Vid->iSliceNumOfCurrPic*sizeof(Slice*));
           //free;
           free(p_Vid->ppSliceList);
           ppSliceList = p_Vid->ppSliceList = tmpSliceList;
         }
         else
         {
           //assert(tmpSliceList == p_Vid->ppSliceList);
           ppSliceList = p_Vid->ppSliceList = tmpSliceList;
           memset(p_Vid->ppSliceList+p_Vid->iSliceNumOfCurrPic, 0, sizeof(Slice*)*MAX_NUM_DECSLICES);
         }
         p_Vid->iNumOfSlicesAllocated += MAX_NUM_DECSLICES;
       }

			 p_Dec->nalu_pos_array_idx--;
       current_header = SOS;       
    }
    else
    {
			//if(p_Vid->iSliceNumOfCurrPic > 0)
			{
	      if(ppSliceList[p_Vid->iSliceNumOfCurrPic-1]->mb_aff_frame_flag)
	       ppSliceList[p_Vid->iSliceNumOfCurrPic-1]->end_mb_nr_plus1 = p_Vid->FrameSizeInMbs/2;
	      else
	       ppSliceList[p_Vid->iSliceNumOfCurrPic-1]->end_mb_nr_plus1 = p_Vid->FrameSizeInMbs/(1+ppSliceList[p_Vid->iSliceNumOfCurrPic-1]->field_pic_flag);
			}
       p_Vid->newframe = 1;
       currSlice->current_slice_nr = 0;
       //keep it in currentslice;
       ppSliceList[p_Vid->iSliceNumOfCurrPic] = p_Vid->pNextSlice;
       p_Vid->pNextSlice = currSlice;

			 p_Dec->nalu_pos_array_idx++;
    }

    copy_slice_info(currSlice, p_Vid->old_slice);
  }
  iRet = current_header;
  init_picture_decoding(p_Vid);

  for(iSliceNo=0; iSliceNo<p_Vid->iSliceNumOfCurrPic; iSliceNo++)
  {
    currSlice = ppSliceList[iSliceNo];
    current_header = currSlice->current_header;
    //p_Vid->currentSlice = currSlice;

    assert(current_header != EOS);
    assert(currSlice->current_slice_nr == iSliceNo);

    init_slice(p_Vid, currSlice);
    decode_slice(currSlice, current_header);

    p_Vid->iNumOfSlicesDecoded++;
    p_Vid->num_dec_mb += currSlice->num_dec_mb;
    //p_Vid->erc_mvperMB += currSlice->erc_mvperMB;
  }

#if MVC_EXTENSION_ENABLE
  p_Vid->last_dec_view_id = p_Vid->dec_picture->view_id;
#endif
  //if(p_Vid->dec_picture->structure == FRAME)
    //p_Vid->last_dec_poc = p_Vid->dec_picture->frame_poc;
  //else if(p_Vid->dec_picture->structure == TOP_FIELD)
    //p_Vid->last_dec_poc = p_Vid->dec_picture->top_poc;
  //else if(p_Vid->dec_picture->structure == BOTTOM_FIELD)
    //p_Vid->last_dec_poc = p_Vid->dec_picture->bottom_poc;
  exit_picture(p_Vid, &p_Vid->dec_picture);
  p_Vid->previous_frame_num = ppSliceList[0]->frame_num;
  return (iRet);
}


/*!
 ***********************************************************************
 * \brief
 *    compute generic SSE
 ***********************************************************************
 */
int64 compute_SSE(imgpel **imgRef, imgpel **imgSrc, int xRef, int xSrc, int ySize, int xSize)
{
  int i, j;
  imgpel *lineRef, *lineSrc;
  int64 distortion = 0;

  for (j = 0; j < ySize; j++)
  {
    lineRef = &imgRef[j][xRef];    
    lineSrc = &imgSrc[j][xSrc];

    for (i = 0; i < xSize; i++)
      distortion += iabs2( *lineRef++ - *lineSrc++ );
  }
  return distortion;
}

/*!
 ************************************************************************
 * \brief
 *    Reads new slice from bit_stream_dec
 ************************************************************************
 */
int read_new_slice(Slice *currSlice)
{
  VideoParameters *p_Vid = currSlice->p_Vid;
  InputParameters *p_Inp = currSlice->p_Inp;

  NALU_t *nalu = p_Vid->nalu; 
  int current_header = 0;
  int BitsUsedByHeader;
  Bitstream *currStream = NULL;

  static NALU_t *pending_nalu = NULL;

  int slice_id_a, slice_id_b, slice_id_c;

  for (;;)
  {
#if (MVC_EXTENSION_ENABLE)
    currSlice->svc_extension_flag = -1;
#endif
    if (!pending_nalu)
    {
      if (0 == read_next_nalu(p_Vid, nalu))
        return EOS;
    }
    else
    {
      nalu = pending_nalu;
      pending_nalu = NULL;
    }

#if (MVC_EXTENSION_ENABLE)
    if(p_Inp->DecodeAllLayers == 1 && (nalu->nal_unit_type == NALU_TYPE_PREFIX || nalu->nal_unit_type == NALU_TYPE_SLC_EXT))
    {
      currStream = currSlice->partArr[0].bitstream;
      currStream->ei_flag = 0;
      currStream->frame_bitoffset = currStream->read_len = 0;
      fast_memcpy (currStream->streamBuffer, &nalu->buf[1], nalu->len-1);
      currStream->code_len = currStream->bitstream_length = RBSPtoSODB(currStream->streamBuffer, nalu->len-1);

      currSlice->svc_extension_flag = read_u_1 ("svc_extension_flag"        , currStream, &p_Dec->UsedBits);

      if(currSlice->svc_extension_flag)
      {
        nal_unit_header_svc_extension();
      }
      else
      {
        nal_unit_header_mvc_extension(&currSlice->NaluHeaderMVCExt, currStream);
        currSlice->NaluHeaderMVCExt.iPrefixNALU = (nalu->nal_unit_type == NALU_TYPE_PREFIX);
      }

      if(nalu->nal_unit_type == NALU_TYPE_SLC_EXT)
      {        
        if(currSlice->svc_extension_flag)
        {
          //to be implemented for Annex G;
        }
        else 
        {
          nalu->nal_unit_type = NALU_TYPE_SLICE; //currSlice->NaluHeaderMVCExt.non_idr_flag==0? NALU_TYPE_IDR: NALU_TYPE_SLICE; 
        }
      }
    }
#endif

process_nalu:
    switch (nalu->nal_unit_type)
    {
    case NALU_TYPE_SLICE:
    case NALU_TYPE_IDR:

			#if 0
      if (/*p_Vid->recovery_point ||*/ nalu->nal_unit_type == NALU_TYPE_IDR)
      {
        if (p_Vid->recovery_point_found == 0)
        {
          if (nalu->nal_unit_type != NALU_TYPE_IDR)
          {
            printf("Warning: Decoding does not start with an IDR picture.\n");
            p_Vid->non_conforming_stream = 1;
          }
          else
            p_Vid->non_conforming_stream = 0;
        }
        p_Vid->recovery_point_found = 1;
      }

      if (p_Vid->recovery_point_found == 0)
        break;
			#endif

      currSlice->idr_flag = (nalu->nal_unit_type == NALU_TYPE_IDR);
      currSlice->nal_reference_idc = nalu->nal_reference_idc;
      currSlice->dp_mode = PAR_DP_1;
      currSlice->max_part_nr = 1;
#if (MVC_EXTENSION_ENABLE)
      if (currSlice->svc_extension_flag != 0)
      {
        currStream = currSlice->partArr[0].bitstream;
        currStream->ei_flag = 0;
        currStream->frame_bitoffset = currStream->read_len = 0;
        fast_memcpy (currStream->streamBuffer, &nalu->buf[1], nalu->len-1);
        currStream->code_len = currStream->bitstream_length = RBSPtoSODB(currStream->streamBuffer, nalu->len-1);
      }
#else   
      currStream = currSlice->partArr[0].bitstream;
      currStream->ei_flag = 0;
      currStream->frame_bitoffset = currStream->read_len = 0;
      memcpy (currStream->streamBuffer, &nalu->buf[1], nalu->len-1);
      currStream->code_len = currStream->bitstream_length = RBSPtoSODB(currStream->streamBuffer, nalu->len-1);
#endif

#if (MVC_EXTENSION_ENABLE)
      if(currSlice->svc_extension_flag == 0)
      {  //MVC
        //if(is_MVC_profile(p_Vid->active_sps->profile_idc))
        //{
          currSlice->view_id = currSlice->NaluHeaderMVCExt.view_id;
          currSlice->inter_view_flag = currSlice->NaluHeaderMVCExt.inter_view_flag;
          currSlice->anchor_pic_flag = currSlice->NaluHeaderMVCExt.anchor_pic_flag;
        //}
      }
      else if(currSlice->svc_extension_flag == -1) //SVC and the normal AVC;
      {
        if(p_Vid->active_subset_sps == NULL)
        {
          currSlice->view_id = GetBaseViewId(p_Vid, &p_Vid->active_subset_sps);
          if(currSlice->NaluHeaderMVCExt.iPrefixNALU >0)
          {
            assert(currSlice->view_id == currSlice->NaluHeaderMVCExt.view_id);
            currSlice->inter_view_flag = currSlice->NaluHeaderMVCExt.inter_view_flag;
            currSlice->anchor_pic_flag = currSlice->NaluHeaderMVCExt.anchor_pic_flag;
          }
          else
          {
            currSlice->inter_view_flag = 1;
            currSlice->anchor_pic_flag = currSlice->idr_flag;
          }
        }
        else
        {
          assert(p_Vid->active_subset_sps->num_views_minus1 >=0);
          // prefix NALU available
          if(currSlice->NaluHeaderMVCExt.iPrefixNALU >0)
          {
            currSlice->view_id = currSlice->NaluHeaderMVCExt.view_id;
            currSlice->inter_view_flag = currSlice->NaluHeaderMVCExt.inter_view_flag;
            currSlice->anchor_pic_flag = currSlice->NaluHeaderMVCExt.anchor_pic_flag;
          }
          else
          { //no prefix NALU;
            currSlice->view_id = p_Vid->active_subset_sps->view_id[0];
            currSlice->inter_view_flag = 1;
            currSlice->anchor_pic_flag = currSlice->idr_flag;
          }
        }
      }
     currSlice->layer_id = currSlice->view_id = GetVOIdx( p_Vid, currSlice->view_id );
#endif

      // Some syntax of the Slice Header depends on the parameter set, which depends on
      // the parameter set ID of the SLice header.  Hence, read the pic_parameter_set_id
      // of the slice header first, then setup the active parameter sets, and then read
      // the rest of the slice header
      BitsUsedByHeader = FirstPartOfSliceHeader(currSlice);
      UseParameterSet (currSlice);
      currSlice->active_sps = p_Vid->active_sps;
      currSlice->active_pps = p_Vid->active_pps;
      currSlice->Transform8x8Mode = p_Vid->active_pps->transform_8x8_mode_flag;
      currSlice->chroma444_not_separate = (p_Vid->active_sps->chroma_format_idc==YUV444)&&((p_Vid->separate_colour_plane_flag == 0));

      BitsUsedByHeader += RestOfSliceHeader (currSlice);
#if (MVC_EXTENSION_ENABLE)
      //if(currSlice->view_id >=0)
      {
        //currSlice->p_Dpb = p_Vid->p_Dpb_layer[currSlice->view_id];
      }
#endif

      //assign_quant_params (currSlice);        

      // if primary slice is replaced with redundant slice, set the correct image type
      //if(currSlice->redundant_pic_cnt && p_Vid->Is_primary_correct==0 && p_Vid->Is_redundant_correct)
      {
        //p_Vid->dec_picture->slice_type = p_Vid->type;
      }

      if(is_new_picture(p_Vid->dec_picture, currSlice, p_Vid->old_slice))
      {
        if(p_Vid->iSliceNumOfCurrPic==0)
          init_picture(p_Vid, currSlice, p_Inp);

        current_header = SOP;
        //check zero_byte if it is also the first NAL unit in the access unit
        CheckZeroByteVCL(p_Vid, nalu);
      }
      else
        current_header = SOS;

      setup_slice_methods(currSlice);

      // From here on, p_Vid->active_sps, p_Vid->active_pps and the slice header are valid
      if (currSlice->mb_aff_frame_flag)
        currSlice->current_mb_nr = currSlice->start_mb_nr << 1;
      else
        currSlice->current_mb_nr = currSlice->start_mb_nr;

      if (p_Vid->active_pps->entropy_coding_mode_flag)
      {
        int ByteStartPosition = currStream->frame_bitoffset/8;
        if (currStream->frame_bitoffset%8 != 0)
        {
          ++ByteStartPosition;
        }
        arideco_start_decoding (&currSlice->partArr[0].de_cabac, currStream->streamBuffer, ByteStartPosition, &currStream->read_len);
      }
      // printf ("read_new_slice: returning %s\n", current_header == SOP?"SOP":"SOS");
      //FreeNALU(nalu);
      //p_Vid->recovery_point = 0;
      return current_header;
      break;
    case NALU_TYPE_DPA:
      //if (p_Vid->recovery_point_found == 0)
        //break;

      // read DP_A
      currSlice->dpB_NotPresent =1; 
      currSlice->dpC_NotPresent =1; 

      currSlice->idr_flag          = FALSE;
      currSlice->nal_reference_idc = nalu->nal_reference_idc;
      currSlice->dp_mode     = PAR_DP_3;
      currSlice->max_part_nr = 3;
      currSlice->ei_flag     = 0;
#if MVC_EXTENSION_ENABLE
      //currSlice->p_Dpb = p_Vid->p_Dpb_layer[0];
#endif
      currStream             = currSlice->partArr[0].bitstream;
      currStream->ei_flag    = 0;
      currStream->frame_bitoffset = currStream->read_len = 0;
      memcpy (currStream->streamBuffer, &nalu->buf[1], nalu->len-1);
      currStream->code_len = currStream->bitstream_length = RBSPtoSODB(currStream->streamBuffer, nalu->len-1);
#if MVC_EXTENSION_ENABLE
      currSlice->view_id = GetBaseViewId(p_Vid, &p_Vid->active_subset_sps);
      currSlice->inter_view_flag = 1;
      currSlice->layer_id = currSlice->view_id = GetVOIdx( p_Vid, currSlice->view_id );
      currSlice->anchor_pic_flag = currSlice->idr_flag;
#endif

      BitsUsedByHeader = FirstPartOfSliceHeader(currSlice);
      UseParameterSet (currSlice);
      currSlice->active_sps = p_Vid->active_sps;
      currSlice->active_pps = p_Vid->active_pps;
      currSlice->Transform8x8Mode = p_Vid->active_pps->transform_8x8_mode_flag;
      currSlice->chroma444_not_separate = (p_Vid->active_sps->chroma_format_idc==YUV444)&&((p_Vid->separate_colour_plane_flag == 0));

      BitsUsedByHeader += RestOfSliceHeader (currSlice);
#if MVC_EXTENSION_ENABLE
      //currSlice->p_Dpb = p_Vid->p_Dpb_layer[currSlice->view_id];
#endif

      //assign_quant_params (currSlice);        


      if(is_new_picture(p_Vid->dec_picture, currSlice, p_Vid->old_slice))
      {
        if(p_Vid->iSliceNumOfCurrPic==0)
          init_picture(p_Vid, currSlice, p_Inp);

        current_header = SOP;
        //check zero_byte if it is also the first NAL unit in the access unit
        CheckZeroByteVCL(p_Vid, nalu);
      }
      else
        current_header = SOS;

      setup_slice_methods(currSlice);

      // From here on, p_Vid->active_sps, p_Vid->active_pps and the slice header are valid
      if (currSlice->mb_aff_frame_flag)
        currSlice->current_mb_nr = currSlice->start_mb_nr << 1;
      else
        currSlice->current_mb_nr = currSlice->start_mb_nr;

      // Now I need to read the slice ID, which depends on the value of
      // redundant_pic_cnt_present_flag

      slice_id_a  = read_ue_v("NALU: DP_A slice_id", currStream, &p_Dec->UsedBits);

      if (p_Vid->active_pps->entropy_coding_mode_flag)
        error ("received data partition with CABAC, this is not allowed", 500);

      // continue with reading next DP
      if (0 == read_next_nalu(p_Vid, nalu))
        return current_header;

      if ( NALU_TYPE_DPB == nalu->nal_unit_type)
      {
        // we got a DPB
        currStream             = currSlice->partArr[1].bitstream;
        currStream->ei_flag    = 0;
        currStream->frame_bitoffset = currStream->read_len = 0;

        memcpy (currStream->streamBuffer, &nalu->buf[1], nalu->len-1);
        currStream->code_len = currStream->bitstream_length = RBSPtoSODB(currStream->streamBuffer, nalu->len-1);

        slice_id_b  = read_ue_v("NALU: DP_B slice_id", currStream, &p_Dec->UsedBits);

        currSlice->dpB_NotPresent = 0; 

        if ((slice_id_b != slice_id_a) || (nalu->lost_packets))
        {
          printf ("Waning: got a data partition B which does not match DP_A (DP loss!)\n");
          currSlice->dpB_NotPresent =1; 
          currSlice->dpC_NotPresent =1; 
        }
        else
        {
          if (p_Vid->active_pps->redundant_pic_cnt_present_flag)
            read_ue_v("NALU: DP_B redundant_pic_cnt", currStream, &p_Dec->UsedBits);

          // we're finished with DP_B, so let's continue with next DP
          if (0 == read_next_nalu(p_Vid, nalu))
            return current_header;
        }
      }
      else
      {
        currSlice->dpB_NotPresent =1; 
      }

      // check if we got DP_C
      if ( NALU_TYPE_DPC == nalu->nal_unit_type)
      {
        currStream             = currSlice->partArr[2].bitstream;
        currStream->ei_flag    = 0;
        currStream->frame_bitoffset = currStream->read_len = 0;

        memcpy (currStream->streamBuffer, &nalu->buf[1], nalu->len-1);
        currStream->code_len = currStream->bitstream_length = RBSPtoSODB(currStream->streamBuffer, nalu->len-1);

        currSlice->dpC_NotPresent = 0;

        slice_id_c  = read_ue_v("NALU: DP_C slice_id", currStream, &p_Dec->UsedBits);
        if ((slice_id_c != slice_id_a)|| (nalu->lost_packets))
        {
          printf ("Warning: got a data partition C which does not match DP_A(DP loss!)\n");
          //currSlice->dpB_NotPresent =1;
          currSlice->dpC_NotPresent =1;
        }

        if (p_Vid->active_pps->redundant_pic_cnt_present_flag)
          read_ue_v("NALU:SLICE_C redudand_pic_cnt", currStream, &p_Dec->UsedBits);
      }
      else
      {
        currSlice->dpC_NotPresent =1;
        pending_nalu = nalu;
      }

      // check if we read anything else than the expected partitions
      if ((nalu->nal_unit_type != NALU_TYPE_DPB) && (nalu->nal_unit_type != NALU_TYPE_DPC) && (!currSlice->dpC_NotPresent))
      {
        // we have a NALI that we can't process here, so restart processing
        goto process_nalu;
        // yes, "goto" should not be used, but it's really the best way here before we restructure the decoding loop
        // (which should be taken care of anyway)
      }

      //FreeNALU(nalu);
      return current_header;
      break;
    case NALU_TYPE_DPB:
      if (p_Inp->silent == FALSE)
      {
        printf ("found data partition B without matching DP A, discarding\n");
      }
      break;
    case NALU_TYPE_DPC:
      if (p_Inp->silent == FALSE)
      {
        printf ("found data partition C without matching DP A, discarding\n");
      }
      break;
    case NALU_TYPE_SEI:
			p_Dec->nalu_pos_array_idx++;
      //printf ("read_new_slice: Found NALU_TYPE_SEI, len %d\n", nalu->len);
      InterpretSEIMessage(nalu->buf,nalu->len,p_Vid, currSlice);
      break;
    case NALU_TYPE_PPS:
			p_Dec->nalu_pos_array_idx++;
      //printf ("Found NALU_TYPE_PPS\n");
      ProcessPPS(p_Vid, nalu);
      break;
    case NALU_TYPE_SPS:
			p_Dec->nalu_pos_array_idx++;
      //printf ("Found NALU_TYPE_SPS\n");
      ProcessSPS(p_Vid, nalu);
      break;
    case NALU_TYPE_AUD:
      //printf ("Found NALU_TYPE_AUD\n");
      //        printf ("read_new_slice: Found 'Access Unit Delimiter' NAL unit, len %d, ignored\n", nalu->len);
      break;
    case NALU_TYPE_EOSEQ:
      //        printf ("read_new_slice: Found 'End of Sequence' NAL unit, len %d, ignored\n", nalu->len);
      break;
    case NALU_TYPE_EOSTREAM:
      //        printf ("read_new_slice: Found 'End of Stream' NAL unit, len %d, ignored\n", nalu->len);
      break;
    case NALU_TYPE_FILL:
      if (p_Inp->silent == FALSE)
      {
        //printf ("read_new_slice: Found NALU_TYPE_FILL, len %d\n", (int) nalu->len);
        //printf ("Skipping these filling bits, proceeding w/ next NALU\n");
      }
      break;
#if (MVC_EXTENSION_ENABLE)
    case NALU_TYPE_VDRD:
      //printf ("Found NALU_TYPE_VDRD\n");
      //        printf ("read_new_slice: Found 'View and Dependency Representation Delimiter' NAL unit, len %d, ignored\n", nalu->len);
      break;
    case NALU_TYPE_PREFIX:
      //printf ("Found NALU_TYPE_PREFIX\n");
      if(currSlice->svc_extension_flag==1)
        prefix_nal_unit_svc();
      break;
    case NALU_TYPE_SUB_SPS:
      //printf ("Found NALU_TYPE_SUB_SPS\n");
      if (p_Inp->DecodeAllLayers== 1)
      {
				p_Dec->nalu_pos_array_idx++;
        ProcessSubsetSPS(p_Vid, nalu);
      }
      else
      {
        if (p_Inp->silent == FALSE)
          printf ("Found Subsequence SPS NALU. Ignoring.\n");
      }
      break;
    case NALU_TYPE_SLC_EXT:
      //printf ("Found NALU_TYPE_SLC_EXT\n");
      if (p_Inp->DecodeAllLayers == 0 &&  (p_Inp->silent == FALSE))
        printf ("Found SVC extension NALU (%d). Ignoring.\n", (int) nalu->nal_unit_type);
      break;
#endif
    default:
      {
        if (p_Inp->silent == FALSE)
          printf ("Found NALU type %d, len %d undefined, ignore NALU, moving on\n", (int) nalu->nal_unit_type, (int) nalu->len);
      }
      break;
    }
  }
}

/*!
 ************************************************************************
 * \brief
 *    finish decoding of a picture, conceal errors and store it
 *    into the DPB
 ************************************************************************
 */
void exit_picture(VideoParameters *p_Vid, StorablePicture **dec_picture)
{
  InputParameters *p_Inp = p_Vid->p_Inp;
  SNRParameters   *snr   = p_Vid->snr;
  char yuv_types[4][6]= {"4:0:0","4:2:0","4:2:2","4:4:4"};
  int structure, frame_poc, slice_type, refpic, qp, pic_num, chroma_format_idc, is_idr;

  int64 tmp_time;                   // time used by decoding the last frame
  char   yuvFormat[10];

  // return if the last picture has already been finished
  if (*dec_picture==NULL || (p_Vid->num_dec_mb != p_Vid->PicSizeInMbs && (p_Vid->yuv_format != YUV444 || !p_Vid->separate_colour_plane_flag)))
  {
    return;
  }

#if 0
  if(!p_Vid->iDeblockMode && (p_Vid->bDeblockEnable & (1<<(*dec_picture)->used_for_reference)))
  {
    //deblocking for frame or field
    if( (p_Vid->separate_colour_plane_flag != 0) )
    {
      int nplane;
      int colour_plane_id = p_Vid->ppSliceList[0]->colour_plane_id;
      for( nplane=0; nplane<MAX_PLANE; ++nplane )
      {
        p_Vid->ppSliceList[0]->colour_plane_id = nplane;
        change_plane_JV( p_Vid, nplane, NULL );
        //DeblockPicture( p_Vid, *dec_picture );
      }
      p_Vid->ppSliceList[0]->colour_plane_id = colour_plane_id;
      make_frame_picture_JV(p_Vid);
    }
    else
    {
      //DeblockPicture( p_Vid, *dec_picture );
    }
  }
  else
  {
    if( (p_Vid->separate_colour_plane_flag != 0) )
    {
      make_frame_picture_JV(p_Vid);
    }
  }
#endif

  //if ((*dec_picture)->mb_aff_frame_flag)
    //MbAffPostProc(p_Vid);

  if (p_Vid->structure == FRAME)         // buffer mgt. for frame mode
    frame_postprocessing(p_Vid);
  else
    field_postprocessing(p_Vid);   // reset all interlaced variables
#if (MVC_EXTENSION_ENABLE)
  //if((*dec_picture)->used_for_reference || ((*dec_picture)->inter_view_flag == 1))
    //pad_dec_picture(p_Vid, *dec_picture);
#else
  //if((*dec_picture)->used_for_reference)
    //pad_dec_picture(p_Vid, *dec_picture);
#endif
  structure  = (*dec_picture)->structure;
  slice_type = (*dec_picture)->slice_type;
  frame_poc  = (*dec_picture)->frame_poc;  
  refpic     = (*dec_picture)->used_for_reference;
  qp         = (*dec_picture)->qp;
  pic_num    = (*dec_picture)->pic_num;
  is_idr     = (*dec_picture)->idr_flag;

  chroma_format_idc = (*dec_picture)->chroma_format_idc;
#if MVC_EXTENSION_ENABLE
  //store_picture_in_dpb(p_Vid->p_Dpb_layer[(*dec_picture)->view_id], *dec_picture);
#else
  //store_picture_in_dpb(p_Vid->p_Dpb_layer[0], *dec_picture);
#endif

  *dec_picture=NULL;

  if (p_Vid->last_has_mmco_5)
  {
    p_Vid->pre_frame_num = 0;
  }

  if (p_Inp->silent == FALSE)
  {
    if (structure==TOP_FIELD || structure==FRAME)
    {
      if(slice_type == I_SLICE && is_idr) // IDR picture
        strcpy(p_Vid->cslice_type,"IDR");
      else if(slice_type == I_SLICE) // I picture
        strcpy(p_Vid->cslice_type," I ");
      else if(slice_type == P_SLICE) // P pictures
        strcpy(p_Vid->cslice_type," P ");
      else if(slice_type == SP_SLICE) // SP pictures
        strcpy(p_Vid->cslice_type,"SP ");
      else if (slice_type == SI_SLICE)
        strcpy(p_Vid->cslice_type,"SI ");
      else if(refpic) // stored B pictures
        strcpy(p_Vid->cslice_type," B ");
      else // B pictures
        strcpy(p_Vid->cslice_type," b ");

      if (structure==FRAME)
      {
        strncat(p_Vid->cslice_type,")       ",8-strlen(p_Vid->cslice_type));
      }
    }
    else if (structure==BOTTOM_FIELD)
    {
      if(slice_type == I_SLICE && is_idr) // IDR picture
        strncat(p_Vid->cslice_type,"|IDR)",8-strlen(p_Vid->cslice_type));
      else if(slice_type == I_SLICE) // I picture
        strncat(p_Vid->cslice_type,"| I )",8-strlen(p_Vid->cslice_type));
      else if(slice_type == P_SLICE) // P pictures
        strncat(p_Vid->cslice_type,"| P )",8-strlen(p_Vid->cslice_type));
      else if(slice_type == SP_SLICE) // SP pictures
        strncat(p_Vid->cslice_type,"|SP )",8-strlen(p_Vid->cslice_type));
      else if (slice_type == SI_SLICE)
        strncat(p_Vid->cslice_type,"|SI )",8-strlen(p_Vid->cslice_type));
      else if(refpic) // stored B pictures
        strncat(p_Vid->cslice_type,"| B )",8-strlen(p_Vid->cslice_type));
      else // B pictures
        strncat(p_Vid->cslice_type,"| b )",8-strlen(p_Vid->cslice_type));   
    }
  }

  if ((structure==FRAME)||structure==BOTTOM_FIELD)
  {
    gettime (&(p_Vid->end_time));              // end time

    tmp_time  = timediff(&(p_Vid->start_time), &(p_Vid->end_time));
    p_Vid->tot_time += tmp_time;
    tmp_time  = timenorm(tmp_time);
    sprintf(yuvFormat,"%s", yuv_types[chroma_format_idc]);

    if (p_Inp->silent == FALSE)
    {
      SNRParameters   *snr = p_Vid->snr;
      //if (p_Vid->p_ref != -1)
        //fprintf(stdout,"%05d(%s%5d %5d %5d %8.4f %8.4f %8.4f  %s %7d\n",
        //p_Vid->frame_no, p_Vid->cslice_type, frame_poc, pic_num, qp, snr->snr[0], snr->snr[1], snr->snr[2], yuvFormat, (int) tmp_time);
      //else
        fprintf(stdout,"%05d(%s%5d %5d %5d                             %s %7d\n",
        p_Vid->frame_no, p_Vid->cslice_type, frame_poc, pic_num, qp, yuvFormat, (int)tmp_time);
    }
    else
      fprintf(stdout,"Completed Decoding frame %05d.\r",snr->frame_ctr);

    //fflush(stdout);

    if(slice_type == I_SLICE || slice_type == SI_SLICE || slice_type == P_SLICE || refpic)   // I or P pictures
    {
#if (MVC_EXTENSION_ENABLE)
      if((p_Vid->ppSliceList[0])->view_id!=0)
#endif
        ++(p_Vid->number);
    }
    else
      ++(p_Vid->Bframe_ctr);    // B pictures
    ++(snr->frame_ctr);

#if (MVC_EXTENSION_ENABLE)
    if ((p_Vid->ppSliceList[0])->view_id != 0)
#endif
      ++(p_Vid->g_nFrame);   
  }

  //p_Vid->currentSlice->current_mb_nr = -4712;   // impossible value for debugging, StW
  //p_Vid->currentSlice->current_slice_nr = 0;
}

/*!
 ************************************************************************
 * \brief
 *    set defaults for old_slice
 *    NAL unit of a picture"
 ************************************************************************
 */
void init_old_slice(OldSliceParams *p_old_slice)
{
  p_old_slice->field_pic_flag = 0;
  p_old_slice->pps_id         = INT_MAX;
  p_old_slice->frame_num      = INT_MAX;
  p_old_slice->nal_ref_idc    = INT_MAX;
  p_old_slice->idr_flag       = FALSE;

  p_old_slice->pic_oder_cnt_lsb          = UINT_MAX;
  p_old_slice->delta_pic_oder_cnt_bottom = INT_MAX;

  p_old_slice->delta_pic_order_cnt[0] = INT_MAX;
  p_old_slice->delta_pic_order_cnt[1] = INT_MAX;
}


void copy_slice_info(Slice *currSlice, OldSliceParams *p_old_slice)
{
  VideoParameters *p_Vid = currSlice->p_Vid;

  p_old_slice->pps_id         = currSlice->pic_parameter_set_id;
  p_old_slice->frame_num      = currSlice->frame_num; //p_Vid->frame_num;
  p_old_slice->field_pic_flag = currSlice->field_pic_flag; //p_Vid->field_pic_flag;

  if(currSlice->field_pic_flag)
  {
    p_old_slice->bottom_field_flag = currSlice->bottom_field_flag;
  }

  p_old_slice->nal_ref_idc = currSlice->nal_reference_idc;
  p_old_slice->idr_flag    = (byte) currSlice->idr_flag;

  if (currSlice->idr_flag)
  {
    p_old_slice->idr_pic_id = currSlice->idr_pic_id;
  }

  if (p_Vid->active_sps->pic_order_cnt_type == 0)
  {
    p_old_slice->pic_oder_cnt_lsb          = currSlice->pic_order_cnt_lsb;
    p_old_slice->delta_pic_oder_cnt_bottom = currSlice->delta_pic_order_cnt_bottom;
  }

  if (p_Vid->active_sps->pic_order_cnt_type == 1)
  {
    p_old_slice->delta_pic_order_cnt[0] = currSlice->delta_pic_order_cnt[0];
    p_old_slice->delta_pic_order_cnt[1] = currSlice->delta_pic_order_cnt[1];
  }
#if (MVC_EXTENSION_ENABLE)
  p_old_slice->view_id = currSlice->view_id;
  p_old_slice->inter_view_flag = currSlice->inter_view_flag; 
  p_old_slice->anchor_pic_flag = currSlice->anchor_pic_flag;
#endif
  p_old_slice->layer_id = currSlice->layer_id;
}

/*!
 ************************************************************************
 * \brief
 *    detect if current slice is "first VCL NAL unit of a picture"
 ************************************************************************
 */
int is_new_picture(StorablePicture *dec_picture, Slice *currSlice, OldSliceParams *p_old_slice)
{
  VideoParameters *p_Vid = currSlice->p_Vid;

  int result=0;

  result |= (NULL==dec_picture);

  result |= (p_old_slice->pps_id != currSlice->pic_parameter_set_id);

  result |= (p_old_slice->frame_num != currSlice->frame_num);

  result |= (p_old_slice->field_pic_flag != currSlice->field_pic_flag);

  if(currSlice->field_pic_flag && p_old_slice->field_pic_flag)
  {
    result |= (p_old_slice->bottom_field_flag != currSlice->bottom_field_flag);
  }

  result |= (p_old_slice->nal_ref_idc != currSlice->nal_reference_idc) && ((p_old_slice->nal_ref_idc == 0) || (currSlice->nal_reference_idc == 0));
  result |= (p_old_slice->idr_flag    != currSlice->idr_flag);

  if (currSlice->idr_flag && p_old_slice->idr_flag)
  {
    result |= (p_old_slice->idr_pic_id != currSlice->idr_pic_id);
  }

  if (p_Vid->active_sps->pic_order_cnt_type == 0)
  {
    result |= (p_old_slice->pic_oder_cnt_lsb          != currSlice->pic_order_cnt_lsb);
    if( p_Vid->active_pps->bottom_field_pic_order_in_frame_present_flag  ==  1 &&  !currSlice->field_pic_flag )
    {
      result |= (p_old_slice->delta_pic_oder_cnt_bottom != currSlice->delta_pic_order_cnt_bottom);
    }
  }

  if (p_Vid->active_sps->pic_order_cnt_type == 1)
  {
    if (!p_Vid->active_sps->delta_pic_order_always_zero_flag)
    {
      result |= (p_old_slice->delta_pic_order_cnt[0] != currSlice->delta_pic_order_cnt[0]);
      if( p_Vid->active_pps->bottom_field_pic_order_in_frame_present_flag  ==  1 &&  !currSlice->field_pic_flag )
      {
        result |= (p_old_slice->delta_pic_order_cnt[1] != currSlice->delta_pic_order_cnt[1]);
      }
    }
  }

#if (MVC_EXTENSION_ENABLE)
  result |= (currSlice->view_id != p_old_slice->view_id);
  result |= (currSlice->inter_view_flag != p_old_slice->inter_view_flag);
  result |= (currSlice->anchor_pic_flag != p_old_slice->anchor_pic_flag);
#endif
  result |= (currSlice->layer_id != p_old_slice->layer_id);
  return result;
}

/*!
 ************************************************************************
 * \brief
 *    Prepare field and frame buffer after frame decoding
 ************************************************************************
 */
void frame_postprocessing(VideoParameters *p_Vid)
{
}

/*!
 ************************************************************************
 * \brief
 *    Prepare field and frame buffer after field decoding
 ************************************************************************
 */
void field_postprocessing(VideoParameters *p_Vid)
{
  p_Vid->number /= 2;
}



/*!
 ************************************************************************
 * \brief
 *    copy StorablePicture *src -> StorablePicture *dst
 *    for 4:4:4 Independent mode
 ************************************************************************
 */
void copy_dec_picture_JV( VideoParameters *p_Vid, StorablePicture *dst, StorablePicture *src )
{
#if 0	
  dst->top_poc              = src->top_poc;
  dst->bottom_poc           = src->bottom_poc;
  dst->frame_poc            = src->frame_poc;
  dst->qp                   = src->qp;
  dst->slice_qp_delta       = src->slice_qp_delta;
  dst->chroma_qp_offset[0]  = src->chroma_qp_offset[0];
  dst->chroma_qp_offset[1]  = src->chroma_qp_offset[1];

  dst->poc                  = src->poc;

  dst->slice_type           = src->slice_type;
  dst->used_for_reference   = src->used_for_reference;
  dst->idr_flag             = src->idr_flag;
  dst->no_output_of_prior_pics_flag = src->no_output_of_prior_pics_flag;
  dst->long_term_reference_flag = src->long_term_reference_flag;
  dst->adaptive_ref_pic_buffering_flag = src->adaptive_ref_pic_buffering_flag;

  dst->dec_ref_pic_marking_buffer = src->dec_ref_pic_marking_buffer;

  dst->mb_aff_frame_flag    = src->mb_aff_frame_flag;
  dst->PicWidthInMbs        = src->PicWidthInMbs;
  dst->pic_num              = src->pic_num;
  dst->frame_num            = src->frame_num;
  dst->recovery_frame       = src->recovery_frame;
  dst->coded_frame          = src->coded_frame;

  dst->chroma_format_idc    = src->chroma_format_idc;

  dst->frame_mbs_only_flag  = src->frame_mbs_only_flag;
  dst->frame_cropping_flag  = src->frame_cropping_flag;

  dst->frame_crop_left_offset   = src->frame_crop_left_offset;
  dst->frame_crop_right_offset  = src->frame_crop_right_offset;
  dst->frame_crop_top_offset    = src->frame_crop_top_offset;
  dst->frame_crop_bottom_offset = src->frame_crop_bottom_offset;

#endif
}

/*!
 ************************************************************************
 * \brief
 *    decodes one slice
 ************************************************************************
 */
void decode_one_slice(Slice *currSlice)
{	
  VideoParameters *p_Vid = currSlice->p_Vid;
  Boolean end_of_slice = FALSE;
  Macroblock *currMB = NULL;
  currSlice->cod_counter=-1;

  if( (p_Vid->separate_colour_plane_flag != 0) )
  {
    change_plane_JV( p_Vid, currSlice->colour_plane_id, currSlice );
  }
  else
  {
    currSlice->mb_data = p_Vid->mb_data;
    currSlice->dec_picture = p_Vid->dec_picture;
    currSlice->siblock = p_Vid->siblock;
    currSlice->ipredmode = p_Vid->ipredmode;
    currSlice->intra_block = p_Vid->intra_block;
  }

  if (currSlice->slice_type == B_SLICE)
  {
    //compute_colocated(currSlice, currSlice->listX);
  }

  //if (currSlice->slice_type != I_SLICE && currSlice->slice_type != SI_SLICE)
    //init_cur_imgy(currSlice,p_Vid); 

  //reset_ec_flags(p_Vid);

  while (end_of_slice == FALSE) // loop over macroblocks
  {

#if TRACE
    //fprintf(p_Dec->p_trace,"\n*********** POC: %i (I/P) MB: %i Slice: %i Type %d **********\n", currSlice->ThisPOC, currSlice->current_mb_nr, currSlice->current_slice_nr, currSlice->slice_type);
#endif

    // Initializes the current macroblock
    start_macroblock(currSlice, &currMB);
    // Get the syntax elements from the NAL
    //read_one_macroblock_i_slice_cabac read_one_macroblock_i_slice_cavlc
    currSlice->read_one_macroblock(currMB);
    //decode_one_macroblock(currMB, currSlice->dec_picture);

    if(currSlice->mb_aff_frame_flag && currMB->mb_field)
    {
      currSlice->num_ref_idx_active[LIST_0] >>= 1;
      currSlice->num_ref_idx_active[LIST_1] >>= 1;
    }

    end_of_slice = exit_macroblock(currSlice, (!currSlice->mb_aff_frame_flag|| currSlice->current_mb_nr%2));
  }
  //reset_ec_flags(p_Vid);
}

#if (MVC_EXTENSION_ENABLE)
int GetVOIdx(VideoParameters *p_Vid, int iViewId)
{
  int iVOIdx = -1;
  int *piViewIdMap;
  if(p_Vid->active_subset_sps)
  {
    piViewIdMap = p_Vid->active_subset_sps->view_id;
    for(iVOIdx = p_Vid->active_subset_sps->num_views_minus1; iVOIdx>=0; iVOIdx--)
      if(piViewIdMap[iVOIdx] == iViewId)
        break;
  }
  else
  {
    subset_seq_parameter_set_rbsp_t *curr_subset_sps;
    int i;

    curr_subset_sps = p_Vid->SubsetSeqParSet;
    for(i=0; i<MAXSPS; i++)
    {
      if(curr_subset_sps->num_views_minus1>=0 && curr_subset_sps->sps.Valid)
      {
        break;
      }
      curr_subset_sps++;
    }

    if( i < MAXSPS )
    {
      p_Vid->active_subset_sps = curr_subset_sps;

      piViewIdMap = p_Vid->active_subset_sps->view_id;
      for(iVOIdx = p_Vid->active_subset_sps->num_views_minus1; iVOIdx>=0; iVOIdx--)
        if(piViewIdMap[iVOIdx] == iViewId)
          break;

      return iVOIdx;
    }
    else
    {
      iVOIdx = 0;
    }
  }

  return iVOIdx;
}

int GetViewIdx(VideoParameters *p_Vid, int iVOIdx)
{
  int iViewIdx = -1;
  int *piViewIdMap;

  if( p_Vid->active_subset_sps )
  {
    assert( p_Vid->active_subset_sps->num_views_minus1 >= iVOIdx && iVOIdx >= 0 );
    piViewIdMap = p_Vid->active_subset_sps->view_id;
    iViewIdx = piViewIdMap[iVOIdx];    
  }

  return iViewIdx;
}
int get_maxViewIdx (VideoParameters *p_Vid, int view_id, int anchor_pic_flag, int listidx)
{
  int VOIdx;
  int maxViewIdx = 0;

  VOIdx = view_id; 
  if(VOIdx >= 0)
  {
    if(anchor_pic_flag)
      maxViewIdx = listidx? p_Vid->active_subset_sps->num_anchor_refs_l1[VOIdx] : p_Vid->active_subset_sps->num_anchor_refs_l0[VOIdx];
    else
      maxViewIdx = listidx? p_Vid->active_subset_sps->num_non_anchor_refs_l1[VOIdx] : p_Vid->active_subset_sps->num_non_anchor_refs_l0[VOIdx];
  }

  return maxViewIdx;
}
#endif
