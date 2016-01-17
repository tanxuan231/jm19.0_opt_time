/*!
 ***********************************************************************
 *  \file
 *      mbuffer_mvc.c
 *
 *  \brief
 *      Frame buffer functions
 *
 *  \author
 *      Main contributors (see contributors.h for copyright, address and affiliation details)
 *      - Athanasios Leontaris            <aleon@dolby.com>
 *      - Karsten Suehring
 *      - Alexis Tourapis                 <alexismt@ieee.org>
 *      - Yuwen He                        <yhe@dolby.com>
 ***********************************************************************
 */

#include <limits.h>

#include "global.h"
#include "header.h"
#include "image.h"
#include "mbuffer.h"
#include "mbuffer_common.h"
#include "mbuffer_mvc.h"
#include "memalloc.h"

#if (MVC_EXTENSION_ENABLE)
/*!
 ************************************************************************
 * \brief
 *    Returns inter-view prediction pic with given targetViewID
 *
 ************************************************************************
 */
static StorablePicture*  get_inter_view_pic(VideoParameters *p_Vid, Slice *currSlice, int targetViewID, int currPOC, int listidx)
{
  unsigned i;
  unsigned int listinterview_size;
  FrameStore **fs_listinterview;

  if (listidx == 0)
  {
    fs_listinterview = currSlice->fs_listinterview0;
    listinterview_size = currSlice->listinterviewidx0; 
  }
  else
  {
    fs_listinterview = currSlice->fs_listinterview1;
    listinterview_size = currSlice->listinterviewidx1; 
  }

  for(i=0; i<listinterview_size; i++)
  {
    if (fs_listinterview[i]->layer_id == GetVOIdx( p_Vid, targetViewID ))
    {
      if(p_Vid->structure==FRAME && fs_listinterview[i]->frame->poc == currPOC)
      {
        return fs_listinterview[i]->frame;
      }
      else if(p_Vid->structure==TOP_FIELD && fs_listinterview[i]->top_field->poc == currPOC)
      {
        return fs_listinterview[i]->top_field;
      }
      else if(p_Vid->structure==BOTTOM_FIELD && fs_listinterview[i]->bottom_field->poc == currPOC)
      {
        return fs_listinterview[i]->bottom_field;
      }
    }
  }

  return NULL;
}

/*!
 ************************************************************************
 * \brief
 *    Generates a alternating field list from a given FrameStore inter-view list
 *
 ************************************************************************
 */
static void gen_pic_list_from_frame_interview_list(PictureStructure currStructure, FrameStore **fs_list, int list_idx, StorablePicture **list, char *list_size)
{
  int i;

  if (currStructure == TOP_FIELD)
  {
    for (i=0; i<list_idx; i++)
    {
      list[(int)(*list_size)] = fs_list[i]->top_field;
      (*list_size)++;
    }
  }
  if (currStructure == BOTTOM_FIELD)
  {
    for (i=0; i<list_idx; i++)
    {
      list[(int)(*list_size)] = fs_list[i]->bottom_field;
      (*list_size)++;
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
void init_lists_i_slice_mvc(Slice *currSlice)
{
  //VideoParameters *p_Vid = currSlice->p_Vid;

  currSlice->listinterviewidx0 = 0;
  currSlice->listinterviewidx1 = 0;

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
void init_lists_p_slice_mvc(Slice *currSlice)
{
  VideoParameters *p_Vid = currSlice->p_Vid;
  DecodedPictureBuffer *p_Dpb = currSlice->p_Dpb;

  unsigned int i;

  int list0idx = 0;
  int listltidx = 0;

  FrameStore **fs_list0;
  FrameStore **fs_listlt;

  int currPOC = currSlice->ThisPOC;
  int anchor_pic_flag = currSlice->anchor_pic_flag;

  currSlice->listinterviewidx0 = 0;
  currSlice->listinterviewidx1 = 0;

  if (currSlice->structure == FRAME)
  {
    for (i=0; i<p_Dpb->ref_frames_in_buffer; i++)
    {
      if (p_Dpb->fs_ref[i]->is_used==3)
      {
        if ((p_Dpb->fs_ref[i]->frame->used_for_reference)&&(!p_Dpb->fs_ref[i]->frame->is_long_term))
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
          currSlice->listX[0][list0idx++]=p_Dpb->fs_ltref[i]->frame;
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

#if !SIMULCAST_ENABLE
  if (currSlice->svc_extension_flag == 0)
  {        
    int curr_view_id = currSlice->layer_id;
    currSlice->fs_listinterview0 = calloc(p_Dpb->size, sizeof (FrameStore*));
    if (NULL==currSlice->fs_listinterview0)
      no_mem_exit("init_lists: fs_listinterview0");
    list0idx = currSlice->listXsize[0];
    if (currSlice->structure == FRAME)
    {
      append_interview_list(p_Vid->p_Dpb_layer[1], 0, 0, currSlice->fs_listinterview0, &currSlice->listinterviewidx0, currPOC, curr_view_id, anchor_pic_flag);
      for (i=0; i<(unsigned int)currSlice->listinterviewidx0; i++)
      {
        currSlice->listX[0][list0idx++]=currSlice->fs_listinterview0[i]->frame;
      }
      currSlice->listXsize[0] = (char) list0idx;
    }
    else
    {
      append_interview_list(p_Vid->p_Dpb_layer[1], currSlice->structure, 0, currSlice->fs_listinterview0, &currSlice->listinterviewidx0, currPOC, curr_view_id, anchor_pic_flag);
      gen_pic_list_from_frame_interview_list(currSlice->structure, currSlice->fs_listinterview0, currSlice->listinterviewidx0, currSlice->listX[0], &currSlice->listXsize[0]);
    }
  }
#endif
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
}

/*!
 ************************************************************************
 * \brief
 *    Initialize reference lists for a B Slice
 *
 ************************************************************************
 */
void init_lists_b_slice_mvc(Slice *currSlice)
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

  int currPOC = currSlice->ThisPOC;
  int anchor_pic_flag = currSlice->anchor_pic_flag;

  currSlice->listinterviewidx0 = 0;
  currSlice->listinterviewidx1 = 0;

  {
    // B-Slice
    if (currSlice->structure == FRAME)
    {
      for (i=0; i<p_Dpb->ref_frames_in_buffer; i++)
      {
        if (p_Dpb->fs_ref[i]->is_used==3)
        {
          if ((p_Dpb->fs_ref[i]->frame->used_for_reference)&&(!p_Dpb->fs_ref[i]->frame->is_long_term))
          {
            if (currSlice->framepoc >= p_Dpb->fs_ref[i]->frame->poc) //!KS use >= for error concealment
              //            if (p_Vid->framepoc > p_Dpb->fs_ref[i]->frame->poc)
            {
              currSlice->listX[0][list0idx++] = p_Dpb->fs_ref[i]->frame;
            }
          }
        }
      }
      qsort((void *)currSlice->listX[0], list0idx, sizeof(StorablePicture*), compare_pic_by_poc_desc);
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

      //      printf("currSlice->listX[0] currPoc=%d (Poc): ", currSlice->framepoc); for (i=0; i<currSlice->listXsize[0]; i++){printf ("%d  ", currSlice->listX[0][i]->poc);} printf("\n");
      //      printf("currSlice->listX[1] currPoc=%d (Poc): ", currSlice->framepoc); for (i=0; i<currSlice->listXsize[1]; i++){printf ("%d  ", currSlice->listX[1][i]->poc);} printf("\n");

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

      //      printf("fs_list0 currPoc=%d (Poc): ", currSlice->ThisPOC); for (i=0; i<list0idx; i++){printf ("%d  ", fs_list0[i]->poc);} printf("\n");
      //      printf("fs_list1 currPoc=%d (Poc): ", currSlice->ThisPOC); for (i=0; i<list0idx; i++){printf ("%d  ", fs_list1[i]->poc);} printf("\n");

      currSlice->listXsize[0] = 0;
      currSlice->listXsize[1] = 0;
      gen_pic_list_from_frame_list(currSlice->structure, fs_list0, list0idx, currSlice->listX[0], &currSlice->listXsize[0], 0);
      gen_pic_list_from_frame_list(currSlice->structure, fs_list1, list0idx, currSlice->listX[1], &currSlice->listXsize[1], 0);

      //      printf("currSlice->listX[0] currPoc=%d (Poc): ", currSlice->framepoc); for (i=0; i<currSlice->listXsize[0]; i++){printf ("%d  ", currSlice->listX[0][i]->poc);} printf("\n");
      //      printf("currSlice->listX[1] currPoc=%d (Poc): ", currSlice->framepoc); for (i=0; i<currSlice->listXsize[1]; i++){printf ("%d  ", currSlice->listX[1][i]->poc);} printf("\n");

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

#if !SIMULCAST_ENABLE
  if (currSlice->svc_extension_flag == 0)
  {
    int curr_view_id = currSlice->view_id;
    // B-Slice
    currSlice->fs_listinterview0 = calloc(p_Dpb->size, sizeof (FrameStore*));
    if (NULL==currSlice->fs_listinterview0)
      no_mem_exit("init_lists: fs_listinterview0");
    currSlice->fs_listinterview1 = calloc(p_Dpb->size, sizeof (FrameStore*));
    if (NULL==currSlice->fs_listinterview1)
      no_mem_exit("init_lists: fs_listinterview1");
    list0idx = currSlice->listXsize[0];

    if (currSlice->structure == FRAME)
    {
      append_interview_list(
        p_Vid->p_Dpb_layer[1],
        0, 0, currSlice->fs_listinterview0, &currSlice->listinterviewidx0, currPOC, curr_view_id, anchor_pic_flag);
      append_interview_list(
        p_Vid->p_Dpb_layer[1],
        0, 1, currSlice->fs_listinterview1, &currSlice->listinterviewidx1, currPOC, curr_view_id, anchor_pic_flag);
      for (i=0; i<(unsigned int)currSlice->listinterviewidx0; i++)
      {
        currSlice->listX[0][list0idx++]=currSlice->fs_listinterview0[i]->frame;
      }
      currSlice->listXsize[0] = (char) list0idx;
      list0idx = currSlice->listXsize[1];
      for (i=0; i<(unsigned int)currSlice->listinterviewidx1; i++)
      {
        currSlice->listX[1][list0idx++] = currSlice->fs_listinterview1[i]->frame;
      }
      currSlice->listXsize[1] = (char) list0idx;
    }
    else
    {
      append_interview_list(
        p_Vid->p_Dpb_layer[1],
        currSlice->structure, 0, currSlice->fs_listinterview0, &currSlice->listinterviewidx0, currPOC, curr_view_id, anchor_pic_flag);
      gen_pic_list_from_frame_interview_list(currSlice->structure, currSlice->fs_listinterview0, currSlice->listinterviewidx0, currSlice->listX[0], &currSlice->listXsize[0]);
      append_interview_list(
        p_Vid->p_Dpb_layer[1],
        currSlice->structure, 1, currSlice->fs_listinterview1, &currSlice->listinterviewidx1, currPOC, curr_view_id, anchor_pic_flag);
      gen_pic_list_from_frame_interview_list(currSlice->structure, currSlice->fs_listinterview1, currSlice->listinterviewidx1, currSlice->listX[1], &currSlice->listXsize[1]);
    }    
  }
#endif
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
}


/*!
 ************************************************************************
 * \brief
 *    Reordering process for inter-view reference pictures
 *
 ************************************************************************
 */
static void reorder_interview(VideoParameters *p_Vid, Slice *currSlice, StorablePicture **RefPicListX, int num_ref_idx_lX_active_minus1, int *refIdxLX, int targetViewID, int currPOC, int listidx)
{
  int cIdx, nIdx;
  StorablePicture *picLX;

  picLX = get_inter_view_pic(p_Vid, currSlice, targetViewID, currPOC, listidx);

  if (picLX)
  {
    for( cIdx = num_ref_idx_lX_active_minus1+1; cIdx > *refIdxLX; cIdx-- )
      RefPicListX[ cIdx ] = RefPicListX[ cIdx - 1];

    RefPicListX[ (*refIdxLX)++ ] = picLX;

    nIdx = *refIdxLX;

    for( cIdx = *refIdxLX; cIdx <= num_ref_idx_lX_active_minus1+1; cIdx++ )
    {
      if((GetViewIdx( p_Vid, RefPicListX[cIdx]->view_id ) != targetViewID) || (RefPicListX[cIdx]->poc != currPOC))
        RefPicListX[ nIdx++ ] = RefPicListX[ cIdx ];
    }
  }
}
#endif
 
