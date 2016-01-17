
/*!
 ***********************************************************************
 *  \file
 *      mbuffer.c
 *
 *  \brief
 *      Frame buffer functions
 *
 *  \author
 *      Main contributors (see contributors.h for copyright, address and affiliation details)
 *      - Karsten Suehring
 *      - Alexis Tourapis                 <alexismt@ieee.org>
 *      - Jill Boyce                      <jill.boyce@thomson.net>
 *      - Saurav K Bandyopadhyay          <saurav@ieee.org>
 *      - Zhenyu Wu                       <Zhenyu.Wu@thomson.net
 *      - Purvin Pandit                   <Purvin.Pandit@thomson.net>
 *      - Yuwen He                        <yhe@dolby.com>
 ***********************************************************************
 */

#include <limits.h>

#include "global.h"
#include "header.h"
#include "image.h"
#include "mbuffer.h"
#include "mbuffer_common.h"
#include "memalloc.h"
#include "output.h"
#include "mbuffer_mvc.h"
#include "fast_memory.h"
#include "input.h"

static void insert_picture_in_dpb    (VideoParameters *p_Vid, FrameStore* fs, StorablePicture* p);
static int output_one_frame_from_dpb (DecodedPictureBuffer *p_Dpb);
static void gen_field_ref_ids        (VideoParameters *p_Vid, StorablePicture *p);

#define MAX_LIST_SIZE 33

/*!
 ************************************************************************
 * \brief
 *    Returns the size of the dpb depending on level and picture size
 *
 *
 ************************************************************************
 */
int getDpbSize(VideoParameters *p_Vid, seq_parameter_set_rbsp_t *active_sps)
{
  int pic_size_mb = (active_sps->pic_width_in_mbs_minus1 + 1) * (active_sps->pic_height_in_map_units_minus1 + 1) * (active_sps->frame_mbs_only_flag?1:2);

  int size = 0;

  switch (active_sps->level_idc)
  {
  case 0:
    // if there is no level defined, we expect experimental usage and return a DPB size of 16
    return 16;
  case 9:
    size = 396;
    break;
  case 10:
    size = 396;
    break;
  case 11:
    if (!is_FREXT_profile(active_sps->profile_idc) && (active_sps->constrained_set3_flag == 1))
      size = 396;
    else
      size = 900;
    break;
  case 12:
    size = 2376;
    break;
  case 13:
    size = 2376;
    break;
  case 20:
    size = 2376;
    break;
  case 21:
    size = 4752;
    break;
  case 22:
    size = 8100;
    break;
  case 30:
    size = 8100;
    break;
  case 31:
    size = 18000;
    break;
  case 32:
    size = 20480;
    break;
  case 40:
    size = 32768;
    break;
  case 41:
    size = 32768;
    break;
  case 42:
    size = 34816;
    break;
  case 50:
    size = 110400;
    break;
  case 51:
    size = 184320;
    break;
  case 52:
    size = 184320;
    break;
  case 60:
  case 61:
  case 62:
    size = 696320;
    break;
  default:
    error ("undefined level", 500);
    break;
  }

  size /= pic_size_mb;
#if MVC_EXTENSION_ENABLE
  if(p_Vid->profile_idc == MVC_HIGH || p_Vid->profile_idc == STEREO_HIGH)
  {
    int num_views = p_Vid->active_subset_sps->num_views_minus1+1;
    size = imin(2*size, imax(1, RoundLog2(num_views))*16)/num_views;
  }
  else
#endif
  {
    size = imin( size, 16);
  }

  if (active_sps->vui_parameters_present_flag && active_sps->vui_seq_parameters.bitstream_restriction_flag)
  {
    int size_vui;
    if ((int)active_sps->vui_seq_parameters.max_dec_frame_buffering > size)
    {
      error ("max_dec_frame_buffering larger than MaxDpbSize", 500);
    }
    size_vui = imax (1, active_sps->vui_seq_parameters.max_dec_frame_buffering);
#ifdef _DEBUG
    if(size_vui < size)
    {
      printf("Warning: max_dec_frame_buffering(%d) is less than DPB size(%d) calculated from Profile/Level.\n", size_vui, size);
    }
#endif
    size = size_vui;    
  }

  return size;
}

/*!
 ************************************************************************
 * \brief
 *    Allocate memory for decoded picture buffer and initialize with sane values.
 *
 ************************************************************************
 */
void init_dpb(VideoParameters *p_Vid, DecodedPictureBuffer *p_Dpb, int type)
{
  unsigned i; 
  seq_parameter_set_rbsp_t *active_sps = p_Vid->active_sps;

  p_Dpb->p_Vid = p_Vid;
  if (p_Dpb->init_done)
  {
    free_dpb(p_Dpb);
  }

  p_Dpb->size = getDpbSize(p_Vid, active_sps) + p_Vid->p_Inp->dpb_plus[type==2? 1: 0];
  p_Dpb->num_ref_frames = active_sps->num_ref_frames; 

#if (MVC_EXTENSION_ENABLE)
  if ((unsigned int)active_sps->max_dec_frame_buffering < active_sps->num_ref_frames)
#else
  if (p_Dpb->size < active_sps->num_ref_frames)
#endif
  {
    error ("DPB size at specified level is smaller than the specified number of reference frames. This is not allowed.\n", 1000);
  }

  p_Dpb->used_size = 0;
  p_Dpb->last_picture = NULL;

  p_Dpb->ref_frames_in_buffer = 0;
  p_Dpb->ltref_frames_in_buffer = 0;

  p_Dpb->fs = calloc(p_Dpb->size, sizeof (FrameStore*));
  if (NULL==p_Dpb->fs)
    no_mem_exit("init_dpb: p_Dpb->fs");

  p_Dpb->fs_ref = calloc(p_Dpb->size, sizeof (FrameStore*));
  if (NULL==p_Dpb->fs_ref)
    no_mem_exit("init_dpb: p_Dpb->fs_ref");

  p_Dpb->fs_ltref = calloc(p_Dpb->size, sizeof (FrameStore*));
  if (NULL==p_Dpb->fs_ltref)
    no_mem_exit("init_dpb: p_Dpb->fs_ltref");

#if (MVC_EXTENSION_ENABLE)
  p_Dpb->fs_ilref = calloc(1, sizeof (FrameStore*));
  if (NULL==p_Dpb->fs_ilref)
    no_mem_exit("init_dpb: p_Dpb->fs_ilref");
#endif

  for (i = 0; i < p_Dpb->size; i++)
  {
    p_Dpb->fs[i]       = alloc_frame_store();
    p_Dpb->fs_ref[i]   = NULL;
    p_Dpb->fs_ltref[i] = NULL;
    p_Dpb->fs[i]->layer_id = MVC_INIT_VIEW_ID;
#if (MVC_EXTENSION_ENABLE)
    p_Dpb->fs[i]->view_id = MVC_INIT_VIEW_ID;
    p_Dpb->fs[i]->inter_view_flag[0] = p_Dpb->fs[i]->inter_view_flag[1] = 0;
    p_Dpb->fs[i]->anchor_pic_flag[0] = p_Dpb->fs[i]->anchor_pic_flag[1] = 0;
#endif
  }
#if (MVC_EXTENSION_ENABLE)
  if (type == 2)
  {
    p_Dpb->fs_ilref[0] = alloc_frame_store();
    // These may need some cleanups
    p_Dpb->fs_ilref[0]->view_id = MVC_INIT_VIEW_ID;
    p_Dpb->fs_ilref[0]->inter_view_flag[0] = p_Dpb->fs_ilref[0]->inter_view_flag[1] = 0;
    p_Dpb->fs_ilref[0]->anchor_pic_flag[0] = p_Dpb->fs_ilref[0]->anchor_pic_flag[1] = 0;
    // given that this is in a different buffer, do we even need proc_flag anymore?    
  }
  else
    p_Dpb->fs_ilref[0] = NULL;
#endif

  /*
  for (i = 0; i < 6; i++)
  {
  currSlice->listX[i] = calloc(MAX_LIST_SIZE, sizeof (StorablePicture*)); // +1 for reordering
  if (NULL==currSlice->listX[i])
  no_mem_exit("init_dpb: currSlice->listX[i]");
  }
  */
  /* allocate a dummy storable picture */
  if(!p_Vid->no_reference_picture)
  {
    p_Vid->no_reference_picture = alloc_storable_picture (p_Vid, FRAME, p_Vid->width, p_Vid->height, p_Vid->width_cr, p_Vid->height_cr, 1);
    p_Vid->no_reference_picture->top_field    = p_Vid->no_reference_picture;
    p_Vid->no_reference_picture->bottom_field = p_Vid->no_reference_picture;
    p_Vid->no_reference_picture->frame        = p_Vid->no_reference_picture;
  }
  p_Dpb->last_output_poc = INT_MIN;

#if (MVC_EXTENSION_ENABLE)
  p_Dpb->last_output_view_id = -1;
#endif

  p_Vid->last_has_mmco_5 = 0;

  p_Dpb->init_done = 1;

  // picture error concealment
  if(p_Vid->conceal_mode !=0 && !p_Vid->last_out_fs)
    p_Vid->last_out_fs = alloc_frame_store();

}

/*!
 ************************************************************************
 * \brief
 *    Free memory for decoded picture buffer.
 ************************************************************************
 */
void free_dpb(DecodedPictureBuffer *p_Dpb)
{
  VideoParameters *p_Vid = p_Dpb->p_Vid;
  unsigned i;
  if (p_Dpb->fs)
  {
    for (i=0; i<p_Dpb->size; i++)
    {
      free_frame_store(p_Dpb->fs[i]);
    }
    free (p_Dpb->fs);
    p_Dpb->fs=NULL;
  }

  if (p_Dpb->fs_ref)
  {
    free (p_Dpb->fs_ref);
  }
  if (p_Dpb->fs_ltref)
  {
    free (p_Dpb->fs_ltref);
  }

#if (MVC_EXTENSION_ENABLE)
  if (p_Dpb->fs_ilref)
  {
    for (i=0; i<1; i++)
    {
      free_frame_store(p_Dpb->fs_ilref[i]);
    }
    free (p_Dpb->fs_ilref);
    p_Dpb->fs_ilref=NULL;
  }

  p_Dpb->last_output_view_id = -1;
#endif

  p_Dpb->last_output_poc = INT_MIN;

  p_Dpb->init_done = 0;

  // picture error concealment
  if(p_Vid->conceal_mode != 0 || p_Vid->last_out_fs)
      free_frame_store(p_Vid->last_out_fs);

  if(p_Vid->no_reference_picture)
  {
    free_storable_picture(p_Vid->no_reference_picture);
    p_Vid->no_reference_picture = NULL;
  }
}


/*!
 ************************************************************************
 * \brief
 *    Allocate memory for decoded picture buffer frame stores and initialize with sane values.
 *
 * \return
 *    the allocated FrameStore structure
 ************************************************************************
 */
FrameStore* alloc_frame_store(void)
{
  FrameStore *f;

  f = calloc (1, sizeof(FrameStore));
  if (NULL==f)
    no_mem_exit("alloc_frame_store: f");

  f->is_used      = 0;
  f->is_reference = 0;
  f->is_long_term = 0;
  f->is_orig_reference = 0;

  f->is_output = 0;

  f->frame        = NULL;;
  f->top_field    = NULL;
  f->bottom_field = NULL;

  return f;
}

void alloc_pic_motion(PicMotionParamsOld *motion, int size_y, int size_x)
{
  motion->mb_field = calloc (size_y * size_x, sizeof(byte));
  if (motion->mb_field == NULL)
    no_mem_exit("alloc_storable_picture: motion->mb_field");
}

/*!
 ************************************************************************
 * \brief
 *    Allocate memory for a stored picture.
 *
 * \param p_Vid
 *    VideoParameters
 * \param structure
 *    picture structure
 * \param size_x
 *    horizontal luma size
 * \param size_y
 *    vertical luma size
 * \param size_x_cr
 *    horizontal chroma size
 * \param size_y_cr
 *    vertical chroma size
 *
 * \return
 *    the allocated StorablePicture structure
 ************************************************************************
 */
StorablePicture* alloc_storable_picture(VideoParameters *p_Vid, PictureStructure structure, int size_x, int size_y, int size_x_cr, int size_y_cr, int is_output)
{
  seq_parameter_set_rbsp_t *active_sps = p_Vid->active_sps;  

  StorablePicture *s;
  int   nplane;

  //printf ("Allocating (%s) picture (x=%d, y=%d, x_cr=%d, y_cr=%d)\n", (type == FRAME)?"FRAME":(type == TOP_FIELD)?"TOP_FIELD":"BOTTOM_FIELD", size_x, size_y, size_x_cr, size_y_cr);

  s = calloc (1, sizeof(StorablePicture));
  if (NULL==s)
    no_mem_exit("alloc_storable_picture: s");

  if (structure!=FRAME)
  {
    size_y    /= 2;
    size_y_cr /= 2;
  }

  s->PicSizeInMbs = (size_x*size_y)/256;
  s->imgUV = NULL;

  get_mem2Dpel_pad (&(s->imgY), size_y, size_x, p_Vid->iLumaPadY, p_Vid->iLumaPadX);
  s->iLumaStride = size_x+2*p_Vid->iLumaPadX;
  s->iLumaExpandedHeight = size_y+2*p_Vid->iLumaPadY;

  if (active_sps->chroma_format_idc != YUV400)
  {
    get_mem3Dpel_pad(&(s->imgUV), 2, size_y_cr, size_x_cr, p_Vid->iChromaPadY, p_Vid->iChromaPadX);
  }

  s->iChromaStride =size_x_cr + 2*p_Vid->iChromaPadX;
  s->iChromaExpandedHeight = size_y_cr + 2*p_Vid->iChromaPadY;
  s->iLumaPadY   = p_Vid->iLumaPadY;
  s->iLumaPadX   = p_Vid->iLumaPadX;
  s->iChromaPadY = p_Vid->iChromaPadY;
  s->iChromaPadX = p_Vid->iChromaPadX;

  s->separate_colour_plane_flag = p_Vid->separate_colour_plane_flag;

  get_mem2Dmp     ( &s->mv_info, (size_y >> BLOCK_SHIFT), (size_x >> BLOCK_SHIFT));
  alloc_pic_motion( &s->motion , (size_y >> BLOCK_SHIFT), (size_x >> BLOCK_SHIFT));

  if( (p_Vid->separate_colour_plane_flag != 0) )
  {
    for( nplane=0; nplane<MAX_PLANE; nplane++ )
    {
      get_mem2Dmp      (&s->JVmv_info[nplane], (size_y >> BLOCK_SHIFT), (size_x >> BLOCK_SHIFT));
      alloc_pic_motion(&s->JVmotion[nplane] , (size_y >> BLOCK_SHIFT), (size_x >> BLOCK_SHIFT));
    }
  }

  s->pic_num   = 0;
  s->frame_num = 0;
  s->long_term_frame_idx = 0;
  s->long_term_pic_num   = 0;
  s->used_for_reference  = 0;
  s->is_long_term        = 0;
  s->non_existing        = 0;
  s->is_output           = 0;
  s->max_slice_id        = 0;
#if (MVC_EXTENSION_ENABLE)
  s->view_id = -1;
#endif

  s->structure=structure;

  s->size_x = size_x;
  s->size_y = size_y;
  s->size_x_cr = size_x_cr;
  s->size_y_cr = size_y_cr;
  s->size_x_m1 = size_x - 1;
  s->size_y_m1 = size_y - 1;
  s->size_x_cr_m1 = size_x_cr - 1;
  s->size_y_cr_m1 = size_y_cr - 1;

  s->top_field    = p_Vid->no_reference_picture;
  s->bottom_field = p_Vid->no_reference_picture;
  s->frame        = p_Vid->no_reference_picture;

  s->dec_ref_pic_marking_buffer = NULL;

  s->coded_frame  = 0;
  s->mb_aff_frame_flag  = 0;

  s->top_poc = s->bottom_poc = s->poc = 0;
  s->seiHasTone_mapping = 0;

  if(!p_Vid->active_sps->frame_mbs_only_flag && structure != FRAME)
  {
    int i, j;
    for(j = 0; j < MAX_NUM_SLICES; j++)
    {
      for (i = 0; i < 2; i++)
      {
        s->listX[j][i] = calloc(MAX_LIST_SIZE, sizeof (StorablePicture*)); // +1 for reordering
        if (NULL==s->listX[j][i])
        no_mem_exit("alloc_storable_picture: s->listX[i]");
      }
    }
  }

  return s;
}

/*!
 ************************************************************************
 * \brief
 *    Free frame store memory.
 *
 * \param p_Vid
 *    VideoParameters
 * \param f
 *    FrameStore to be freed
 *
 ************************************************************************
 */
void free_frame_store(FrameStore* f)
{
  if (f)
  {
    if (f->frame)
    {
      free_storable_picture(f->frame);
      f->frame=NULL;
    }
    if (f->top_field)
    {
      free_storable_picture(f->top_field);
      f->top_field=NULL;
    }
    if (f->bottom_field)
    {
      free_storable_picture(f->bottom_field);
      f->bottom_field=NULL;
    }
    free(f);
  }
}

void free_pic_motion(PicMotionParamsOld *motion)
{
  if (motion->mb_field)
  {
    free(motion->mb_field);
    motion->mb_field = NULL;
  }
}


/*!
 ************************************************************************
 * \brief
 *    Free picture memory.
 *
 * \param p
 *    Picture to be freed
 *
 ************************************************************************
 */
void free_storable_picture(StorablePicture* p)
{
  int nplane;
  if (p)
  {
    if (p->mv_info)
    {
      free_mem2Dmp(p->mv_info);
      p->mv_info = NULL;
    }
    free_pic_motion(&p->motion);

    if( (p->separate_colour_plane_flag != 0) )
    {
      for( nplane=0; nplane<MAX_PLANE; nplane++ )
      {
        if (p->JVmv_info[nplane])
        {
          free_mem2Dmp(p->JVmv_info[nplane]);
          p->JVmv_info[nplane] = NULL;
        }
        free_pic_motion(&p->JVmotion[nplane]);
      }
    }

    if (p->imgY)
    {
      free_mem2Dpel_pad(p->imgY, p->iLumaPadY, p->iLumaPadX);
      p->imgY = NULL;
    }

    if (p->imgUV)
    {
      free_mem3Dpel_pad(p->imgUV, 2, p->iChromaPadY, p->iChromaPadX);
      p->imgUV=NULL;
    }


    if (p->seiHasTone_mapping)
      free(p->tone_mapping_lut);

    {
      int i, j;
      for(j = 0; j < MAX_NUM_SLICES; j++)
      {
        for(i=0; i<2; i++)
        {
          if(p->listX[j][i])
          {
            free(p->listX[j][i]);
            p->listX[j][i] = NULL;
          }
        }
      }
    }
    free(p);
    p = NULL;
  }
}

/*!
 ************************************************************************
 * \brief
 *    mark FrameStore unused for reference
 *
 ************************************************************************
 */
void unmark_for_reference(FrameStore* fs)
{
  if (fs->is_used & 1)
  {
    if (fs->top_field)
    {
      fs->top_field->used_for_reference = 0;
    }
  }
  if (fs->is_used & 2)
  {
    if (fs->bottom_field)
    {
      fs->bottom_field->used_for_reference = 0;
    }
  }
  if (fs->is_used == 3)
  {
    if (fs->top_field && fs->bottom_field)
    {
      fs->top_field->used_for_reference = 0;
      fs->bottom_field->used_for_reference = 0;
    }
    fs->frame->used_for_reference = 0;
  }

  fs->is_reference = 0;

  if(fs->frame)
  {
    free_pic_motion(&fs->frame->motion);
  }

  if (fs->top_field)
  {
    free_pic_motion(&fs->top_field->motion);
  }

  if (fs->bottom_field)
  {
    free_pic_motion(&fs->bottom_field->motion);
  }
}


/*!
 ************************************************************************
 * \brief
 *    mark FrameStore unused for reference and reset long term flags
 *
 ************************************************************************
 */
void unmark_for_long_term_reference(FrameStore* fs)
{
  if (fs->is_used & 1)
  {
    if (fs->top_field)
    {
      fs->top_field->used_for_reference = 0;
      fs->top_field->is_long_term = 0;
    }
  }
  if (fs->is_used & 2)
  {
    if (fs->bottom_field)
    {
      fs->bottom_field->used_for_reference = 0;
      fs->bottom_field->is_long_term = 0;
    }
  }
  if (fs->is_used == 3)
  {
    if (fs->top_field && fs->bottom_field)
    {
      fs->top_field->used_for_reference = 0;
      fs->top_field->is_long_term = 0;
      fs->bottom_field->used_for_reference = 0;
      fs->bottom_field->is_long_term = 0;
    }
    fs->frame->used_for_reference = 0;
    fs->frame->is_long_term = 0;
  }

  fs->is_reference = 0;
  fs->is_long_term = 0;
}



void update_pic_num(Slice *currSlice)
{
  unsigned int i;
  VideoParameters *p_Vid = currSlice->p_Vid;
  DecodedPictureBuffer *p_Dpb = currSlice->p_Dpb;
  seq_parameter_set_rbsp_t *active_sps = p_Vid->active_sps;

  int add_top = 0, add_bottom = 0;
  int max_frame_num = 1 << (active_sps->log2_max_frame_num_minus4 + 4);

  if (currSlice->structure == FRAME)
  {
    for (i=0; i<p_Dpb->ref_frames_in_buffer; i++)
    {
      if ( p_Dpb->fs_ref[i]->is_used==3 )
      {
        if ((p_Dpb->fs_ref[i]->frame->used_for_reference)&&(!p_Dpb->fs_ref[i]->frame->is_long_term))
        {
          if( p_Dpb->fs_ref[i]->frame_num > currSlice->frame_num )
          {
            p_Dpb->fs_ref[i]->frame_num_wrap = p_Dpb->fs_ref[i]->frame_num - max_frame_num;
          }
          else
          {
            p_Dpb->fs_ref[i]->frame_num_wrap = p_Dpb->fs_ref[i]->frame_num;
          }
          p_Dpb->fs_ref[i]->frame->pic_num = p_Dpb->fs_ref[i]->frame_num_wrap;
        }
      }
    }
    // update long_term_pic_num
    for (i = 0; i < p_Dpb->ltref_frames_in_buffer; i++)
    {
      if (p_Dpb->fs_ltref[i]->is_used==3)
      {
        if (p_Dpb->fs_ltref[i]->frame->is_long_term)
        {
          p_Dpb->fs_ltref[i]->frame->long_term_pic_num = p_Dpb->fs_ltref[i]->frame->long_term_frame_idx;
        }
      }
    }
  }
  else
  {
    if (currSlice->structure == TOP_FIELD)
    {
      add_top    = 1;
      add_bottom = 0;
    }
    else
    {
      add_top    = 0;
      add_bottom = 1;
    }

    for (i=0; i<p_Dpb->ref_frames_in_buffer; i++)
    {
      if (p_Dpb->fs_ref[i]->is_reference)
      {
        if( p_Dpb->fs_ref[i]->frame_num > currSlice->frame_num )
        {
          p_Dpb->fs_ref[i]->frame_num_wrap = p_Dpb->fs_ref[i]->frame_num - max_frame_num;
        }
        else
        {
          p_Dpb->fs_ref[i]->frame_num_wrap = p_Dpb->fs_ref[i]->frame_num;
        }
        if (p_Dpb->fs_ref[i]->is_reference & 1)
        {
          p_Dpb->fs_ref[i]->top_field->pic_num = (2 * p_Dpb->fs_ref[i]->frame_num_wrap) + add_top;
        }
        if (p_Dpb->fs_ref[i]->is_reference & 2)
        {
          p_Dpb->fs_ref[i]->bottom_field->pic_num = (2 * p_Dpb->fs_ref[i]->frame_num_wrap) + add_bottom;
        }
      }
    }
    // update long_term_pic_num
    for (i=0; i<p_Dpb->ltref_frames_in_buffer; i++)
    {
      if (p_Dpb->fs_ltref[i]->is_long_term & 1)
      {
        p_Dpb->fs_ltref[i]->top_field->long_term_pic_num = 2 * p_Dpb->fs_ltref[i]->top_field->long_term_frame_idx + add_top;
      }
      if (p_Dpb->fs_ltref[i]->is_long_term & 2)
      {
        p_Dpb->fs_ltref[i]->bottom_field->long_term_pic_num = 2 * p_Dpb->fs_ltref[i]->bottom_field->long_term_frame_idx + add_bottom;
      }
    }
  }
}
/*!
 ************************************************************************
 * \brief
 *    Initialize reference lists depending on current slice type
 *
 ************************************************************************
 */
void init_lists_i_slice(Slice *currSlice)
{

#if (MVC_EXTENSION_ENABLE)
  currSlice->listinterviewidx0 = 0;
  currSlice->listinterviewidx1 = 0;
#endif

  currSlice->listXsize[0] = 0;
  currSlice->listXsize[1] = 0;
}

/*!
 ************************************************************************
 * \brief
 *    Initialize reference lists for a P Slice
 *
 ************************************************************************
 */
void init_lists_p_slice(Slice *currSlice)
{
  VideoParameters *p_Vid = currSlice->p_Vid;
  DecodedPictureBuffer *p_Dpb = currSlice->p_Dpb;

  unsigned int i;

  int list0idx = 0;
  int listltidx = 0;

  FrameStore **fs_list0;
  FrameStore **fs_listlt;

#if (MVC_EXTENSION_ENABLE)
  currSlice->listinterviewidx0 = 0;
  currSlice->listinterviewidx1 = 0;
#endif

  if (currSlice->structure == FRAME)
  {
    for (i=0; i<p_Dpb->ref_frames_in_buffer; i++)
    {
      if (p_Dpb->fs_ref[i]->is_used==3)
      {
        if ((p_Dpb->fs_ref[i]->frame->used_for_reference) && (!p_Dpb->fs_ref[i]->frame->is_long_term))
        {
          currSlice->listX[0][list0idx++] = p_Dpb->fs_ref[i]->frame;
        }
      }
    }
    // order list 0 by PicNum
    qsort((void *)currSlice->listX[0], list0idx, sizeof(StorablePicture*), compare_pic_by_pic_num_desc);
    currSlice->listXsize[0] = (char) list0idx;
    //printf("listX[0] (PicNum): "); for (i=0; i<list0idx; i++){printf ("%d  ", currSlice->listX[0][i]->pic_num);} printf("\n");

    // long term handling
    for (i=0; i<p_Dpb->ltref_frames_in_buffer; i++)
    {
      if (p_Dpb->fs_ltref[i]->is_used==3)
      {
        if (p_Dpb->fs_ltref[i]->frame->is_long_term)
        {
          currSlice->listX[0][list0idx++] = p_Dpb->fs_ltref[i]->frame;
        }
      }
    }
    qsort((void *)&currSlice->listX[0][(short) currSlice->listXsize[0]], list0idx - currSlice->listXsize[0], sizeof(StorablePicture*), compare_pic_by_lt_pic_num_asc);
    currSlice->listXsize[0] = (char) list0idx;
  }
  else
  {
    fs_list0 = calloc(p_Dpb->size, sizeof (FrameStore*));
    if (NULL==fs_list0)
      no_mem_exit("init_lists: fs_list0");
    fs_listlt = calloc(p_Dpb->size, sizeof (FrameStore*));
    if (NULL==fs_listlt)
      no_mem_exit("init_lists: fs_listlt");

    for (i=0; i<p_Dpb->ref_frames_in_buffer; i++)
    {
      if (p_Dpb->fs_ref[i]->is_reference)
      {
        fs_list0[list0idx++] = p_Dpb->fs_ref[i];
      }
    }

    qsort((void *)fs_list0, list0idx, sizeof(FrameStore*), compare_fs_by_frame_num_desc);

    //printf("fs_list0 (FrameNum): "); for (i=0; i<list0idx; i++){printf ("%d  ", fs_list0[i]->frame_num_wrap);} printf("\n");

    currSlice->listXsize[0] = 0;
    gen_pic_list_from_frame_list(currSlice->structure, fs_list0, list0idx, currSlice->listX[0], &currSlice->listXsize[0], 0);

    //printf("listX[0] (PicNum): "); for (i=0; i < currSlice->listXsize[0]; i++){printf ("%d  ", currSlice->listX[0][i]->pic_num);} printf("\n");

    // long term handling
    for (i=0; i<p_Dpb->ltref_frames_in_buffer; i++)
    {
      fs_listlt[listltidx++]=p_Dpb->fs_ltref[i];
    }

    qsort((void *)fs_listlt, listltidx, sizeof(FrameStore*), compare_fs_by_lt_pic_idx_asc);

    gen_pic_list_from_frame_list(currSlice->structure, fs_listlt, listltidx, currSlice->listX[0], &currSlice->listXsize[0], 1);

    free(fs_list0);
    free(fs_listlt);
  }
  currSlice->listXsize[1] = 0;


  // set max size
  currSlice->listXsize[0] = (char) imin (currSlice->listXsize[0], currSlice->num_ref_idx_active[LIST_0]);
  currSlice->listXsize[1] = (char) imin (currSlice->listXsize[1], currSlice->num_ref_idx_active[LIST_1]);

  // set the unused list entries to NULL
  for (i=currSlice->listXsize[0]; i< (MAX_LIST_SIZE) ; i++)
  {
    currSlice->listX[0][i] = p_Vid->no_reference_picture;
  }
  for (i=currSlice->listXsize[1]; i< (MAX_LIST_SIZE) ; i++)
  {
    currSlice->listX[1][i] = p_Vid->no_reference_picture;
  }

#if PRINTREFLIST
#if (MVC_EXTENSION_ENABLE)
  // print out for debug purpose
  if((p_Vid->profile_idc == MVC_HIGH || p_Vid->profile_idc == STEREO_HIGH) && currSlice->current_slice_nr==0)
  {
    if(currSlice->listXsize[0]>0)
    {
      printf("\n");
      printf(" ** (CurViewID:%d %d) %s Ref Pic List 0 ****\n", currSlice->view_id, currSlice->ThisPOC, currSlice->structure==FRAME ? "FRM":(currSlice->structure==TOP_FIELD ? "TOP":"BOT"));
      for(i=0; i<(unsigned int)(currSlice->listXsize[0]); i++)  //ref list 0
      {
        printf("   %2d -> POC: %4d PicNum: %4d ViewID: %d\n", i, currSlice->listX[0][i]->poc, currSlice->listX[0][i]->pic_num, currSlice->listX[0][i]->view_id);
      }
    }
  }
#endif
#endif
}


/*!
 ************************************************************************
 * \brief
 *    Initialize reference lists for a B Slice
 *
 ************************************************************************
 */
void init_lists_b_slice(Slice *currSlice)
{
  VideoParameters *p_Vid = currSlice->p_Vid;
  DecodedPictureBuffer *p_Dpb = currSlice->p_Dpb;

  unsigned int i;
  int j;

  int list0idx = 0;
  int list0idx_1 = 0;
  int listltidx = 0;

  FrameStore **fs_list0;
  FrameStore **fs_list1;
  FrameStore **fs_listlt;

#if (MVC_EXTENSION_ENABLE)
  currSlice->listinterviewidx0 = 0;
  currSlice->listinterviewidx1 = 0;
#endif

  {
    // B-Slice
    if (currSlice->structure == FRAME)
    {
      for (i = 0; i < p_Dpb->ref_frames_in_buffer; i++)
      {
        if (p_Dpb->fs_ref[i]->is_used==3)
        {
          if ((p_Dpb->fs_ref[i]->frame->used_for_reference)&&(!p_Dpb->fs_ref[i]->frame->is_long_term))
          {
            if (currSlice->framepoc >= p_Dpb->fs_ref[i]->frame->poc) //!KS use >= for error concealment
            {
              currSlice->listX[0][list0idx++] = p_Dpb->fs_ref[i]->frame;
            }
          }
        }
      }
      qsort((void *)currSlice->listX[0], list0idx, sizeof(StorablePicture*), compare_pic_by_poc_desc);

      //get the backward reference picture (POC>current POC) in list0;
      list0idx_1 = list0idx;
      for (i=0; i<p_Dpb->ref_frames_in_buffer; i++)
      {
        if (p_Dpb->fs_ref[i]->is_used==3)
        {
          if ((p_Dpb->fs_ref[i]->frame->used_for_reference)&&(!p_Dpb->fs_ref[i]->frame->is_long_term))
          {
            if (currSlice->framepoc < p_Dpb->fs_ref[i]->frame->poc)
            {
              currSlice->listX[0][list0idx++] = p_Dpb->fs_ref[i]->frame;
            }
          }
        }
      }
      qsort((void *)&currSlice->listX[0][list0idx_1], list0idx-list0idx_1, sizeof(StorablePicture*), compare_pic_by_poc_asc);

      for (j=0; j<list0idx_1; j++)
      {
        currSlice->listX[1][list0idx-list0idx_1+j]=currSlice->listX[0][j];
      }
      for (j=list0idx_1; j<list0idx; j++)
      {
        currSlice->listX[1][j-list0idx_1]=currSlice->listX[0][j];
      }

      currSlice->listXsize[0] = currSlice->listXsize[1] = (char) list0idx;

      //printf("listX[0] (PicNum): "); for (i=0; i<currSlice->listXsize[0]; i++){printf ("%d  ", currSlice->listX[0][i]->pic_num);} printf("\n");
      //printf("listX[1] (PicNum): "); for (i=0; i<currSlice->listXsize[1]; i++){printf ("%d  ", currSlice->listX[1][i]->pic_num);} printf("\n");
      //printf("currSlice->listX[0] currPoc=%d (Poc): ", p_Vid->framepoc); for (i=0; i<currSlice->listXsize[0]; i++){printf ("%d  ", currSlice->listX[0][i]->poc);} printf("\n");
      //printf("currSlice->listX[1] currPoc=%d (Poc): ", p_Vid->framepoc); for (i=0; i<currSlice->listXsize[1]; i++){printf ("%d  ", currSlice->listX[1][i]->poc);} printf("\n");

      // long term handling
      for (i=0; i<p_Dpb->ltref_frames_in_buffer; i++)
      {
        if (p_Dpb->fs_ltref[i]->is_used==3)
        {
          if (p_Dpb->fs_ltref[i]->frame->is_long_term)
          {
            currSlice->listX[0][list0idx]   = p_Dpb->fs_ltref[i]->frame;
            currSlice->listX[1][list0idx++] = p_Dpb->fs_ltref[i]->frame;
          }
        }
      }
      qsort((void *)&currSlice->listX[0][(short) currSlice->listXsize[0]], list0idx - currSlice->listXsize[0], sizeof(StorablePicture*), compare_pic_by_lt_pic_num_asc);
      qsort((void *)&currSlice->listX[1][(short) currSlice->listXsize[0]], list0idx - currSlice->listXsize[0], sizeof(StorablePicture*), compare_pic_by_lt_pic_num_asc);
      currSlice->listXsize[0] = currSlice->listXsize[1] = (char) list0idx;
    }
    else
    {
      fs_list0 = calloc(p_Dpb->size, sizeof (FrameStore*));
      if (NULL==fs_list0)
        no_mem_exit("init_lists: fs_list0");
      fs_list1 = calloc(p_Dpb->size, sizeof (FrameStore*));
      if (NULL==fs_list1)
        no_mem_exit("init_lists: fs_list1");
      fs_listlt = calloc(p_Dpb->size, sizeof (FrameStore*));
      if (NULL==fs_listlt)
        no_mem_exit("init_lists: fs_listlt");

      currSlice->listXsize[0] = 0;
      currSlice->listXsize[1] = 1;

      for (i=0; i<p_Dpb->ref_frames_in_buffer; i++)
      {
        if (p_Dpb->fs_ref[i]->is_used)
        {
          if (currSlice->ThisPOC >= p_Dpb->fs_ref[i]->poc)
          {
            fs_list0[list0idx++] = p_Dpb->fs_ref[i];
          }
        }
      }
      qsort((void *)fs_list0, list0idx, sizeof(FrameStore*), compare_fs_by_poc_desc);
      list0idx_1 = list0idx;
      for (i=0; i<p_Dpb->ref_frames_in_buffer; i++)
      {
        if (p_Dpb->fs_ref[i]->is_used)
        {
          if (currSlice->ThisPOC < p_Dpb->fs_ref[i]->poc)
          {
            fs_list0[list0idx++] = p_Dpb->fs_ref[i];
          }
        }
      }
      qsort((void *)&fs_list0[list0idx_1], list0idx-list0idx_1, sizeof(FrameStore*), compare_fs_by_poc_asc);

      for (j=0; j<list0idx_1; j++)
      {
        fs_list1[list0idx-list0idx_1+j]=fs_list0[j];
      }
      for (j=list0idx_1; j<list0idx; j++)
      {
        fs_list1[j-list0idx_1]=fs_list0[j];
      }

      //printf("fs_list0 currPoc=%d (Poc): ", currSlice->ThisPOC); for (i=0; i<list0idx; i++){printf ("%d  ", fs_list0[i]->poc);} printf("\n");
      //printf("fs_list1 currPoc=%d (Poc): ", currSlice->ThisPOC); for (i=0; i<list0idx; i++){printf ("%d  ", fs_list1[i]->poc);} printf("\n");

      currSlice->listXsize[0] = 0;
      currSlice->listXsize[1] = 0;
      gen_pic_list_from_frame_list(currSlice->structure, fs_list0, list0idx, currSlice->listX[0], &currSlice->listXsize[0], 0);
      gen_pic_list_from_frame_list(currSlice->structure, fs_list1, list0idx, currSlice->listX[1], &currSlice->listXsize[1], 0);

      //printf("currSlice->listX[0] currPoc=%d (Poc): ", p_Vid->framepoc); for (i=0; i<currSlice->listXsize[0]; i++){printf ("%d  ", currSlice->listX[0][i]->poc);} printf("\n");
      //printf("currSlice->listX[1] currPoc=%d (Poc): ", p_Vid->framepoc); for (i=0; i<currSlice->listXsize[1]; i++){printf ("%d  ", currSlice->listX[1][i]->poc);} printf("\n");

      // long term handling
      for (i=0; i<p_Dpb->ltref_frames_in_buffer; i++)
      {
        fs_listlt[listltidx++]=p_Dpb->fs_ltref[i];
      }

      qsort((void *)fs_listlt, listltidx, sizeof(FrameStore*), compare_fs_by_lt_pic_idx_asc);

      gen_pic_list_from_frame_list(currSlice->structure, fs_listlt, listltidx, currSlice->listX[0], &currSlice->listXsize[0], 1);
      gen_pic_list_from_frame_list(currSlice->structure, fs_listlt, listltidx, currSlice->listX[1], &currSlice->listXsize[1], 1);

      free(fs_list0);
      free(fs_list1);
      free(fs_listlt);
    }
  }

  if ((currSlice->listXsize[0] == currSlice->listXsize[1]) && (currSlice->listXsize[0] > 1))
  {
    // check if lists are identical, if yes swap first two elements of currSlice->listX[1]
    int diff=0;
    for (j = 0; j< currSlice->listXsize[0]; j++)
    {
      if (currSlice->listX[0][j] != currSlice->listX[1][j])
      {
        diff = 1;
        break;
      }
    }
    if (!diff)
    {
      StorablePicture *tmp_s = currSlice->listX[1][0];
      currSlice->listX[1][0]=currSlice->listX[1][1];
      currSlice->listX[1][1]=tmp_s;
    }
  }

  // set max size
  currSlice->listXsize[0] = (char) imin (currSlice->listXsize[0], currSlice->num_ref_idx_active[LIST_0]);
  currSlice->listXsize[1] = (char) imin (currSlice->listXsize[1], currSlice->num_ref_idx_active[LIST_1]);

  // set the unused list entries to NULL
  for (i=currSlice->listXsize[0]; i< (MAX_LIST_SIZE) ; i++)
  {
    currSlice->listX[0][i] = p_Vid->no_reference_picture;
  }
  for (i=currSlice->listXsize[1]; i< (MAX_LIST_SIZE) ; i++)
  {
    currSlice->listX[1][i] = p_Vid->no_reference_picture;
  }

#if PRINTREFLIST
#if (MVC_EXTENSION_ENABLE)
  // print out for debug purpose
  if((p_Vid->profile_idc == MVC_HIGH || p_Vid->profile_idc == STEREO_HIGH) && currSlice->current_slice_nr==0)
  {
    if((currSlice->listXsize[0]>0) || (currSlice->listXsize[1]>0))
      printf("\n");
    if(currSlice->listXsize[0]>0)
    {
      printf(" ** (CurViewID:%d %d) %s Ref Pic List 0 ****\n", currSlice->view_id, currSlice->ThisPOC, currSlice->structure==FRAME ? "FRM":(currSlice->structure==TOP_FIELD ? "TOP":"BOT"));
      for(i=0; i<(unsigned int)(currSlice->listXsize[0]); i++)  //ref list 0
      {
        printf("   %2d -> POC: %4d PicNum: %4d ViewID: %d\n", i, currSlice->listX[0][i]->poc, currSlice->listX[0][i]->pic_num, currSlice->listX[0][i]->view_id);
      }
    }
    if(currSlice->listXsize[1]>0)
    {
      printf(" ** (CurViewID:%d %d) %s Ref Pic List 1 ****\n", currSlice->view_id, currSlice->ThisPOC, currSlice->structure==FRAME ? "FRM":(currSlice->structure==TOP_FIELD ? "TOP":"BOT"));
      for(i=0; i<(unsigned int)(currSlice->listXsize[1]); i++)  //ref list 1
      {
        printf("   %2d -> POC: %4d PicNum: %4d ViewID: %d\n", i, currSlice->listX[1][i]->poc, currSlice->listX[1][i]->pic_num, currSlice->listX[1][i]->view_id);
      }
    }
  }
#endif
#endif
}

/*!
 ************************************************************************
 * \brief
 *    Initialize listX[2..5] from lists 0 and 1
 *    listX[2]: list0 for current_field==top
 *    listX[3]: list1 for current_field==top
 *    listX[4]: list0 for current_field==bottom
 *    listX[5]: list1 for current_field==bottom
 *
 ************************************************************************
 */
void init_mbaff_lists(VideoParameters *p_Vid, Slice *currSlice)
{
  unsigned j;
  int i;

  for (i=2;i<6;i++)
  {
    for (j=0; j<MAX_LIST_SIZE; j++)
    {
      currSlice->listX[i][j] = p_Vid->no_reference_picture;
    }
    currSlice->listXsize[i]=0;
  }

  for (i = 0; i < currSlice->listXsize[0]; i++)
  {
    currSlice->listX[2][2*i  ] = currSlice->listX[0][i]->top_field;
    currSlice->listX[2][2*i+1] = currSlice->listX[0][i]->bottom_field;
    currSlice->listX[4][2*i  ] = currSlice->listX[0][i]->bottom_field;
    currSlice->listX[4][2*i+1] = currSlice->listX[0][i]->top_field;
  }
  currSlice->listXsize[2] = currSlice->listXsize[4] = currSlice->listXsize[0] * 2;

  for (i = 0; i < currSlice->listXsize[1]; i++)
  {
    currSlice->listX[3][2*i  ] = currSlice->listX[1][i]->top_field;
    currSlice->listX[3][2*i+1] = currSlice->listX[1][i]->bottom_field;
    currSlice->listX[5][2*i  ] = currSlice->listX[1][i]->bottom_field;
    currSlice->listX[5][2*i+1] = currSlice->listX[1][i]->top_field;
  }
  currSlice->listXsize[3] = currSlice->listXsize[5] = currSlice->listXsize[1] * 2;
}

/*!
 ************************************************************************
 * \brief
 *    Generate a frame from top and bottom fields,
 *    YUV components and display information only
 ************************************************************************
 */
void dpb_combine_field_yuv(VideoParameters *p_Vid, FrameStore *fs)
{
  int i, j;

  if (!fs->frame)
  {
    fs->frame = alloc_storable_picture(p_Vid, FRAME, fs->top_field->size_x, fs->top_field->size_y*2, fs->top_field->size_x_cr, fs->top_field->size_y_cr*2, 1);
  }

  for (i=0; i<fs->top_field->size_y; i++)
  {
    memcpy(fs->frame->imgY[i*2],     fs->top_field->imgY[i]   , fs->top_field->size_x * sizeof(imgpel));     // top field
    memcpy(fs->frame->imgY[i*2 + 1], fs->bottom_field->imgY[i], fs->bottom_field->size_x * sizeof(imgpel)); // bottom field
  }

  for (j = 0; j < 2; j++)
  {
    for (i=0; i<fs->top_field->size_y_cr; i++)
    {
      memcpy(fs->frame->imgUV[j][i*2],     fs->top_field->imgUV[j][i],    fs->top_field->size_x_cr*sizeof(imgpel));
      memcpy(fs->frame->imgUV[j][i*2 + 1], fs->bottom_field->imgUV[j][i], fs->bottom_field->size_x_cr*sizeof(imgpel));
    }
  }
  fs->poc=fs->frame->poc =fs->frame->frame_poc = imin (fs->top_field->poc, fs->bottom_field->poc);

  fs->bottom_field->frame_poc=fs->top_field->frame_poc=fs->frame->poc;

  fs->bottom_field->top_poc=fs->frame->top_poc=fs->top_field->poc;
  fs->top_field->bottom_poc=fs->frame->bottom_poc=fs->bottom_field->poc;

  fs->frame->used_for_reference = (fs->top_field->used_for_reference && fs->bottom_field->used_for_reference );
  fs->frame->is_long_term = (fs->top_field->is_long_term && fs->bottom_field->is_long_term );

  if (fs->frame->is_long_term)
    fs->frame->long_term_frame_idx = fs->long_term_frame_idx;

  fs->frame->top_field    = fs->top_field;
  fs->frame->bottom_field = fs->bottom_field;
  fs->frame->frame = fs->frame;

  fs->frame->coded_frame = 0;

  fs->frame->chroma_format_idc = fs->top_field->chroma_format_idc;
  fs->frame->frame_cropping_flag = fs->top_field->frame_cropping_flag;
  if (fs->frame->frame_cropping_flag)
  {
    fs->frame->frame_crop_top_offset = fs->top_field->frame_crop_top_offset;
    fs->frame->frame_crop_bottom_offset = fs->top_field->frame_crop_bottom_offset;
    fs->frame->frame_crop_left_offset = fs->top_field->frame_crop_left_offset;
    fs->frame->frame_crop_right_offset = fs->top_field->frame_crop_right_offset;
  }

  fs->top_field->frame = fs->bottom_field->frame = fs->frame;
  fs->top_field->top_field = fs->top_field;
  fs->top_field->bottom_field = fs->bottom_field;
  fs->bottom_field->top_field = fs->top_field;
  fs->bottom_field->bottom_field = fs->bottom_field;
  if(fs->top_field->used_for_reference || fs->bottom_field->used_for_reference)
  {
    pad_dec_picture(p_Vid, fs->frame);
  }

}


/*!
 ************************************************************************
 * \brief
 *    Generate a frame from top and bottom fields
 ************************************************************************
 */
void dpb_combine_field(VideoParameters *p_Vid, FrameStore *fs)
{
  int i,j, jj, jj4, k, l;

  dpb_combine_field_yuv(p_Vid, fs);

#if (MVC_EXTENSION_ENABLE)
  fs->frame->view_id = fs->view_id;
#endif
  fs->frame->iCodingType = fs->top_field->iCodingType; //FIELD_CODING;
   //! Use inference flag to remap mvs/references

  //! Generate Frame parameters from field information.

  for (j=0 ; j < (fs->top_field->size_y >> 2) ; j++)
  {
    jj = (j<<1);
    jj4 = jj + 1;
    for (i=0 ; i< (fs->top_field->size_x >> 2) ; i++)
    {
      fs->frame->mv_info[jj][i].mv[LIST_0] = fs->top_field->mv_info[j][i].mv[LIST_0];
      fs->frame->mv_info[jj][i].mv[LIST_1] = fs->top_field->mv_info[j][i].mv[LIST_1];

      fs->frame->mv_info[jj][i].ref_idx[LIST_0] = fs->top_field->mv_info[j][i].ref_idx[LIST_0];
      fs->frame->mv_info[jj][i].ref_idx[LIST_1] = fs->top_field->mv_info[j][i].ref_idx[LIST_1];

      /* bug: top field list doesnot exist.*/
      l = fs->top_field->mv_info[j][i].slice_no;
      k = fs->top_field->mv_info[j][i].ref_idx[LIST_0];
      fs->frame->mv_info[jj][i].ref_pic[LIST_0] = k>=0? fs->top_field->listX[l][LIST_0][k]: NULL;  
      k = fs->top_field->mv_info[j][i].ref_idx[LIST_1];
      fs->frame->mv_info[jj][i].ref_pic[LIST_1] = k>=0? fs->top_field->listX[l][LIST_1][k]: NULL;

      //! association with id already known for fields.
      fs->frame->mv_info[jj4][i].mv[LIST_0] = fs->bottom_field->mv_info[j][i].mv[LIST_0];
      fs->frame->mv_info[jj4][i].mv[LIST_1] = fs->bottom_field->mv_info[j][i].mv[LIST_1];

      fs->frame->mv_info[jj4][i].ref_idx[LIST_0]  = fs->bottom_field->mv_info[j][i].ref_idx[LIST_0];
      fs->frame->mv_info[jj4][i].ref_idx[LIST_1]  = fs->bottom_field->mv_info[j][i].ref_idx[LIST_1];
      l = fs->bottom_field->mv_info[j][i].slice_no;

      k = fs->bottom_field->mv_info[j][i].ref_idx[LIST_0];
      fs->frame->mv_info[jj4][i].ref_pic[LIST_0] = k>=0? fs->bottom_field->listX[l][LIST_0][k]: NULL;
      k = fs->bottom_field->mv_info[j][i].ref_idx[LIST_1];
      fs->frame->mv_info[jj4][i].ref_pic[LIST_1] = k>=0? fs->bottom_field->listX[l][LIST_1][k]: NULL;
    }
  }
}


/*!
 ************************************************************************
 * \brief
 *    Allocate memory for buffering of reference picture reordering commands
 ************************************************************************
 */
void alloc_ref_pic_list_reordering_buffer(Slice *currSlice)
{
  if (currSlice->slice_type != I_SLICE && currSlice->slice_type != SI_SLICE)
  {
    int size = currSlice->num_ref_idx_active[LIST_0] + 1;
    if ((currSlice->modification_of_pic_nums_idc[LIST_0] = calloc(size ,sizeof(int)))==NULL) 
       no_mem_exit("alloc_ref_pic_list_reordering_buffer: modification_of_pic_nums_idc_l0");
    if ((currSlice->abs_diff_pic_num_minus1[LIST_0] = calloc(size,sizeof(int)))==NULL) 
       no_mem_exit("alloc_ref_pic_list_reordering_buffer: abs_diff_pic_num_minus1_l0");
    if ((currSlice->long_term_pic_idx[LIST_0] = calloc(size,sizeof(int)))==NULL) 
       no_mem_exit("alloc_ref_pic_list_reordering_buffer: long_term_pic_idx_l0");
#if (MVC_EXTENSION_ENABLE)
    if ((currSlice->abs_diff_view_idx_minus1[LIST_0] = calloc(size,sizeof(int)))==NULL) 
       no_mem_exit("alloc_ref_pic_list_reordering_buffer: abs_diff_view_idx_minus1_l0");
#endif
  }
  else
  {
    currSlice->modification_of_pic_nums_idc[LIST_0] = NULL;
    currSlice->abs_diff_pic_num_minus1[LIST_0] = NULL;
    currSlice->long_term_pic_idx[LIST_0] = NULL;
#if (MVC_EXTENSION_ENABLE)
    currSlice->abs_diff_view_idx_minus1[LIST_0] = NULL;
#endif
  }

  if (currSlice->slice_type == B_SLICE)
  {
    int size = currSlice->num_ref_idx_active[LIST_1] + 1;
    if ((currSlice->modification_of_pic_nums_idc[LIST_1] = calloc(size,sizeof(int)))==NULL) 
      no_mem_exit("alloc_ref_pic_list_reordering_buffer: modification_of_pic_nums_idc_l1");
    if ((currSlice->abs_diff_pic_num_minus1[LIST_1] = calloc(size,sizeof(int)))==NULL) 
      no_mem_exit("alloc_ref_pic_list_reordering_buffer: abs_diff_pic_num_minus1_l1");
    if ((currSlice->long_term_pic_idx[LIST_1] = calloc(size,sizeof(int)))==NULL) 
      no_mem_exit("alloc_ref_pic_list_reordering_buffer: long_term_pic_idx_l1");
#if (MVC_EXTENSION_ENABLE)
    if ((currSlice->abs_diff_view_idx_minus1[LIST_1] = calloc(size,sizeof(int)))==NULL) 
      no_mem_exit("alloc_ref_pic_list_reordering_buffer: abs_diff_view_idx_minus1_l1");
#endif
  }
  else
  {
    currSlice->modification_of_pic_nums_idc[LIST_1] = NULL;
    currSlice->abs_diff_pic_num_minus1[LIST_1] = NULL;
    currSlice->long_term_pic_idx[LIST_1] = NULL;
#if (MVC_EXTENSION_ENABLE)
    currSlice->abs_diff_view_idx_minus1[LIST_1] = NULL;
#endif
  }
}


/*!
 ************************************************************************
 * \brief
 *    Free memory for buffering of reference picture reordering commands
 ************************************************************************
 */
void free_ref_pic_list_reordering_buffer(Slice *currSlice)
{
  if (currSlice->modification_of_pic_nums_idc[LIST_0])
    free(currSlice->modification_of_pic_nums_idc[LIST_0]);
  if (currSlice->abs_diff_pic_num_minus1[LIST_0])
    free(currSlice->abs_diff_pic_num_minus1[LIST_0]);
  if (currSlice->long_term_pic_idx[LIST_0])
    free(currSlice->long_term_pic_idx[LIST_0]);

  currSlice->modification_of_pic_nums_idc[LIST_0] = NULL;
  currSlice->abs_diff_pic_num_minus1[LIST_0] = NULL;
  currSlice->long_term_pic_idx[LIST_0] = NULL;

  if (currSlice->modification_of_pic_nums_idc[LIST_1])
    free(currSlice->modification_of_pic_nums_idc[LIST_1]);
  if (currSlice->abs_diff_pic_num_minus1[LIST_1])
    free(currSlice->abs_diff_pic_num_minus1[LIST_1]);
  if (currSlice->long_term_pic_idx[LIST_1])
    free(currSlice->long_term_pic_idx[LIST_1]);

  currSlice->modification_of_pic_nums_idc[LIST_1] = NULL;
  currSlice->abs_diff_pic_num_minus1[LIST_1] = NULL;
  currSlice->long_term_pic_idx[LIST_1] = NULL;

#if (MVC_EXTENSION_ENABLE)
  if (currSlice->abs_diff_view_idx_minus1[LIST_0])
    free(currSlice->abs_diff_view_idx_minus1[LIST_0]);
  currSlice->abs_diff_view_idx_minus1[LIST_0] = NULL;
  if (currSlice->abs_diff_view_idx_minus1[LIST_1])
    free(currSlice->abs_diff_view_idx_minus1[LIST_1]);
  currSlice->abs_diff_view_idx_minus1[LIST_1] = NULL;
#endif
}

/*!
 ************************************************************************
 * \brief
 *    Compute co-located motion info
 *
 ************************************************************************
 */
void compute_colocated (Slice *currSlice, StorablePicture **listX[6])
{
  int i, j;

  VideoParameters *p_Vid = currSlice->p_Vid;

  if (currSlice->direct_spatial_mv_pred_flag == 0)
  {
    for (j = 0; j < 2 + (currSlice->mb_aff_frame_flag * 4); j += 2)
    {
      for (i = 0; i < currSlice->listXsize[j];i++)
      {
        int prescale, iTRb, iTRp;

        if (j==0)
        {
          iTRb = iClip3( -128, 127, p_Vid->dec_picture->poc - listX[LIST_0 + j][i]->poc );
        }
        else if (j == 2)
        {
          iTRb = iClip3( -128, 127, p_Vid->dec_picture->top_poc - listX[LIST_0 + j][i]->poc );
        }
        else
        {
          iTRb = iClip3( -128, 127, p_Vid->dec_picture->bottom_poc - listX[LIST_0 + j][i]->poc );
        }

        iTRp = iClip3( -128, 127,  listX[LIST_1 + j][0]->poc - listX[LIST_0 + j][i]->poc);

        if (iTRp!=0)
        {
          prescale = ( 16384 + iabs( iTRp / 2 ) ) / iTRp;
          currSlice->mvscale[j][i] = iClip3( -1024, 1023, ( iTRb * prescale + 32 ) >> 6 ) ;
        }
        else
        {
          currSlice->mvscale[j][i] = 9999;
        }
      }
    }
  }
}


#if (MVC_EXTENSION_ENABLE)
int GetMaxDecFrameBuffering(VideoParameters *p_Vid)
{
  int i, j, iMax, iMax_1 = 0, iMax_2 = 0;
  subset_seq_parameter_set_rbsp_t *curr_subset_sps;
  seq_parameter_set_rbsp_t *curr_sps;

  curr_subset_sps = p_Vid->SubsetSeqParSet;
  curr_sps = p_Vid->SeqParSet;
  for(i=0; i<MAXSPS; i++)
  {
    if(curr_subset_sps->Valid && curr_subset_sps->sps.seq_parameter_set_id < MAXSPS)
    {
      j = curr_subset_sps->sps.max_dec_frame_buffering;

      if (curr_subset_sps->sps.vui_parameters_present_flag && curr_subset_sps->sps.vui_seq_parameters.bitstream_restriction_flag)
      {
        if ((int)curr_subset_sps->sps.vui_seq_parameters.max_dec_frame_buffering > j)
        {
          error ("max_dec_frame_buffering larger than MaxDpbSize", 500);
        }
        j = imax (1, curr_subset_sps->sps.vui_seq_parameters.max_dec_frame_buffering);
      }

      if(j > iMax_2)
        iMax_2 = j;
    }
    
    if(curr_sps->Valid)
    {
      j = curr_sps->max_dec_frame_buffering;

      if (curr_sps->vui_parameters_present_flag && curr_sps->vui_seq_parameters.bitstream_restriction_flag)
      {
        if ((int)curr_sps->vui_seq_parameters.max_dec_frame_buffering > j)
        {
          error ("max_dec_frame_buffering larger than MaxDpbSize", 500);
        }
        j = imax (1, curr_sps->vui_seq_parameters.max_dec_frame_buffering);
      }

      if(j > iMax_1)
        iMax_1 = j;
    }
    curr_subset_sps++;
    curr_sps++;
  }  
      
  if (iMax_1 > 0 && iMax_2 > 0)
    iMax = iMax_1 + iMax_2;
  else
    iMax = (iMax_1 >0? iMax_1*2 : iMax_2*2);
  return iMax;
}

static int is_view_id_in_ref_view_list(int view_id, int *ref_view_id, int num_ref_views)
{
   int i;
   for(i=0; i<num_ref_views; i++)
   {
     if(view_id == ref_view_id[i])
       break;
   }

   return (num_ref_views && (i<num_ref_views));
}

void append_interview_list(DecodedPictureBuffer *p_Dpb, 
                           PictureStructure currPicStructure, //0: frame; 1:top field; 2: bottom field;
                           int list_idx, 
                           FrameStore **list,
                           int *listXsize, 
                           int currPOC, 
                           int curr_view_id, 
                           int anchor_pic_flag)
{
  VideoParameters *p_Vid = p_Dpb->p_Vid;
  int iVOIdx = curr_view_id;
  int pic_avail;
  int poc = 0;
  int fld_idx;
  int num_ref_views, *ref_view_id;
  FrameStore *fs = p_Dpb->fs_ilref[0];


  if(iVOIdx <0)
    printf("Error: iVOIdx: %d is not less than 0\n", iVOIdx);

  if(anchor_pic_flag)
  {
    num_ref_views = list_idx? p_Vid->active_subset_sps->num_anchor_refs_l1[iVOIdx] : p_Vid->active_subset_sps->num_anchor_refs_l0[iVOIdx];
    ref_view_id   = list_idx? p_Vid->active_subset_sps->anchor_ref_l1[iVOIdx]:p_Vid->active_subset_sps->anchor_ref_l0[iVOIdx];
  }
  else
  {
    num_ref_views = list_idx? p_Vid->active_subset_sps->num_non_anchor_refs_l1[iVOIdx] : p_Vid->active_subset_sps->num_non_anchor_refs_l0[iVOIdx];
    ref_view_id = list_idx? p_Vid->active_subset_sps->non_anchor_ref_l1[iVOIdx]:p_Vid->active_subset_sps->non_anchor_ref_l0[iVOIdx];
  }

  //  if(num_ref_views <= 0)
  //    printf("Error: iNumOfRefViews: %d is not larger than 0\n", num_ref_views);

  if(currPicStructure == BOTTOM_FIELD)
    fld_idx = 1;
  else
    fld_idx = 0;

    if(currPicStructure==FRAME)
    {
      pic_avail = (fs->is_used == 3);
      if (pic_avail)
        poc = fs->frame->poc;
    }
    else if(currPicStructure==TOP_FIELD)
    {
      pic_avail = fs->is_used & 1;
      if (pic_avail)
        poc = fs->top_field->poc;
    }
    else if(currPicStructure==BOTTOM_FIELD)
    {
      pic_avail = fs->is_used & 2;
      if (pic_avail)
        poc = fs->bottom_field->poc;
    }
    else
      pic_avail =0;

    if(pic_avail && fs->inter_view_flag[fld_idx])
    {
      if(poc == currPOC)
      {
        if(is_view_id_in_ref_view_list(fs->view_id, ref_view_id, num_ref_views))
        {
          //add one inter-view reference;
          list[*listXsize] = fs; 
          //next;
          (*listXsize)++;
        }
      }
    }
}

#endif

int init_img_data(VideoParameters *p_Vid, ImageData *p_ImgData, seq_parameter_set_rbsp_t *sps)
{
  InputParameters *p_Inp = p_Vid->p_Inp;
  int memory_size = 0;
  int nplane;
  
  // allocate memory for reference frame buffers: p_ImgData->frm_data
  p_ImgData->format           = p_Inp->output;
  p_ImgData->format.width[0]  = p_Vid->width;    
  p_ImgData->format.width[1]  = p_Vid->width_cr;
  p_ImgData->format.width[2]  = p_Vid->width_cr;
  p_ImgData->format.height[0] = p_Vid->height;  
  p_ImgData->format.height[1] = p_Vid->height_cr;
  p_ImgData->format.height[2] = p_Vid->height_cr;
  p_ImgData->format.yuv_format          = (ColorFormat) sps->chroma_format_idc;
  p_ImgData->format.auto_crop_bottom    = p_Inp->output.auto_crop_bottom;
  p_ImgData->format.auto_crop_right     = p_Inp->output.auto_crop_right;
  p_ImgData->format.auto_crop_bottom_cr = p_Inp->output.auto_crop_bottom_cr;
  p_ImgData->format.auto_crop_right_cr  = p_Inp->output.auto_crop_right_cr;
  p_ImgData->frm_stride[0]    = p_Vid->width;
  p_ImgData->frm_stride[1]    = p_ImgData->frm_stride[2] = p_Vid->width_cr;
  p_ImgData->top_stride[0] = p_ImgData->bot_stride[0] = p_ImgData->frm_stride[0] << 1;
  p_ImgData->top_stride[1] = p_ImgData->top_stride[2] = p_ImgData->bot_stride[1] = p_ImgData->bot_stride[2] = p_ImgData->frm_stride[1] << 1;

  if( sps->separate_colour_plane_flag )
  {
    for( nplane=0; nplane < MAX_PLANE; nplane++ )
    {
      memory_size += get_mem2Dpel(&(p_ImgData->frm_data[nplane]), p_Vid->height, p_Vid->width);
    }
  }
  else
  {
    memory_size += get_mem2Dpel(&(p_ImgData->frm_data[0]), p_Vid->height, p_Vid->width);

    if (p_Vid->yuv_format != YUV400)
    {
      int i, j, k;
      memory_size += get_mem2Dpel(&(p_ImgData->frm_data[1]), p_Vid->height_cr, p_Vid->width_cr);
      memory_size += get_mem2Dpel(&(p_ImgData->frm_data[2]), p_Vid->height_cr, p_Vid->width_cr);

      if (sizeof(imgpel) == sizeof(unsigned char))
      {
        for (k = 1; k < 3; k++)
          fast_memset(p_ImgData->frm_data[k][0], 128, p_Vid->height_cr * p_Vid->width_cr * sizeof(imgpel));
      }
      else
      {
        imgpel mean_val;

        for (k = 1; k < 3; k++)
        {
          mean_val = (imgpel) ((p_Vid->max_pel_value_comp[k] + 1) >> 1);

          for (j = 0; j < p_Vid->height_cr; j++)
          {
            for (i = 0; i < p_Vid->width_cr; i++)
              p_ImgData->frm_data[k][j][i] = mean_val;
          }
        }
      }
    }
  }

  if (!p_Vid->active_sps->frame_mbs_only_flag)
  {
    // allocate memory for field reference frame buffers
    memory_size += init_top_bot_planes(p_ImgData->frm_data[0], p_Vid->height, &(p_ImgData->top_data[0]), &(p_ImgData->bot_data[0]));

    if (p_Vid->yuv_format != YUV400)
    {
      memory_size += 4*(sizeof(imgpel**));

      memory_size += init_top_bot_planes(p_ImgData->frm_data[1], p_Vid->height_cr, &(p_ImgData->top_data[1]), &(p_ImgData->bot_data[1]));
      memory_size += init_top_bot_planes(p_ImgData->frm_data[2], p_Vid->height_cr, &(p_ImgData->top_data[2]), &(p_ImgData->bot_data[2]));
    }
  }

  return memory_size;
}

void free_img_data(VideoParameters *p_Vid, ImageData *p_ImgData)
{
  if ( p_Vid->separate_colour_plane_flag )
  {
    int nplane;

    for( nplane=0; nplane<MAX_PLANE; nplane++ )
    {
      if (p_ImgData->frm_data[nplane])
      {
        free_mem2Dpel(p_ImgData->frm_data[nplane]);      // free ref frame buffers
        p_ImgData->frm_data[nplane] = NULL;
      }
    }
  }
  else
  {
    if (p_ImgData->frm_data[0])
    {
      free_mem2Dpel(p_ImgData->frm_data[0]);      // free ref frame buffers
      p_ImgData->frm_data[0] = NULL;
    }
    
    if (p_ImgData->format.yuv_format != YUV400)
    {
      if (p_ImgData->frm_data[1])
      {
        free_mem2Dpel(p_ImgData->frm_data[1]);
        p_ImgData->frm_data[1] = NULL;
      }
      if (p_ImgData->frm_data[2])
      {
        free_mem2Dpel(p_ImgData->frm_data[2]);
        p_ImgData->frm_data[2] = NULL;
      }
    }
  }
  
  if (!p_Vid->active_sps->frame_mbs_only_flag)
  {
    free_top_bot_planes(p_ImgData->top_data[0], p_ImgData->bot_data[0]);

    if (p_ImgData->format.yuv_format != YUV400)
    {
      free_top_bot_planes(p_ImgData->top_data[1], p_ImgData->bot_data[1]);
      free_top_bot_planes(p_ImgData->top_data[2], p_ImgData->bot_data[2]);
    }
  }
}

static inline void copy_img_data(imgpel *out_img, imgpel *in_img, int ostride, int istride, unsigned int size_y, unsigned int size_x)
{
  unsigned int i;
  for(i = 0; i < size_y; i++)
  {
    fast_memcpy(out_img, in_img, size_x);
    out_img += ostride;
    in_img += istride;
  }
}

