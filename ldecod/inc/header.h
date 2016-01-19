/*!
 *************************************************************************************
 * \file header.h
 *
 * \brief
 *    Prototypes for header.c
 *************************************************************************************
 */

#ifndef _HEADER_H_
#define _HEADER_H_

extern int FirstPartOfSliceHeader(Slice *currSlice);
extern int RestOfSliceHeader     (Slice *currSlice);

extern void dec_ref_pic_marking(VideoParameters *p_Vid, Bitstream *currStream, Slice *pSlice);
#endif

