
/*!
 *************************************************************************************
 * \file img_io.c
 *
 * \brief
 *    image I/O related functions
 *
 * \author
 *    Main contributors (see contributors.h for copyright, address and affiliation details)
 *     - Alexis Michael Tourapis         <alexismt@ieee.org>
 *************************************************************************************
 */
#include "contributors.h"
#include "global.h"
#include "img_io.h"

/*!
 ************************************************************************
 * \brief
 *    Open file containing a single frame
 ************************************************************************
 */
void OpenFrameFile( VideoDataFile *input_file, int FrameNumberInFile)
{
  char infile [FILE_NAME_SIZE], in_number[16];
  infile[FILE_NAME_SIZE-1]='\0';
  strncpy(infile, input_file->fhead, FILE_NAME_SIZE-1);

  if (input_file->zero_pad)       
    snprintf(in_number, 16, "%0*d", input_file->num_digits, FrameNumberInFile);
  else
    snprintf(in_number, 16, "%*d", input_file->num_digits, FrameNumberInFile);

  strncat(infile, in_number, FILE_NAME_SIZE-strlen(in_number)-1);
  strncat(infile, input_file->ftail, FILE_NAME_SIZE-strlen(input_file->ftail)-1);

  if ((input_file->f_num = open(infile, OPENFLAGS_READ)) == -1)
  {
    printf ("OpenFrameFile: cannot open file %s\n", infile);
    report_stats_on_error();
  }    
}
