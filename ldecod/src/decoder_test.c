
/*!
 ***********************************************************************
 *  \file
 *     decoder_test.c
 *  \brief
 *     H.264/AVC decoder test 
 *  \author
 *     Main contributors (see contributors.h for copyright, address and affiliation details)
 *     - Yuwen He       <yhe@dolby.com>
 ***********************************************************************
 */

#include "contributors.h"

#include <sys/stat.h>

//#include "global.h"
#include "win32.h"
#include "h264decoder.h"
#include "configfile.h"

#define DECOUTPUT_TEST      0

#define PRINT_OUTPUT_POC    0
#define BITSTREAM_FILENAME  "test.264"
#define DECRECON_FILENAME   "test_dec.yuv"
#define ENCRECON_FILENAME   "test_rec.yuv"
#define FCFR_DEBUG_FILENAME "fcfr_dec_rpu_stats.txt"
#define DECOUTPUT_VIEW0_FILENAME  "H264_Decoder_Output_View0.yuv"
#define DECOUTPUT_VIEW1_FILENAME  "H264_Decoder_Output_View1.yuv"


static void Configure(InputParameters *p_Inp, int ac, char *av[])
{
  //char *config_filename=NULL;
  //char errortext[ET_SIZE];
  memset(p_Inp, 0, sizeof(InputParameters));
  strcpy(p_Inp->infile, BITSTREAM_FILENAME); //! set default bitstream name
  strcpy(p_Inp->outfile, DECRECON_FILENAME); //! set default output file name
  strcpy(p_Inp->reffile, ENCRECON_FILENAME); //! set default reference file name
  
#ifdef _LEAKYBUCKET_
  strcpy(p_Inp->LeakyBucketParamFile,"leakybucketparam.cfg");    // file where Leaky Bucket parameters (computed by encoder) are stored
#endif

  ParseCommand(p_Inp, ac, av);

  fprintf(stdout,"----------------------------- JM %s %s -----------------------------\n", VERSION, EXT_VERSION);
  //fprintf(stdout," Decoder config file                    : %s \n",config_filename);
  if(!p_Inp->bDisplayDecParams)
  {
    fprintf(stdout,"--------------------------------------------------------------------------\n");
    fprintf(stdout," Input H.264 bitstream                  : %s \n",p_Inp->infile);
    fprintf(stdout," Output decoded YUV                     : %s \n",p_Inp->outfile);
    //fprintf(stdout," Output status file                     : %s \n",LOGFILE);
    fprintf(stdout," Input reference file                   : %s \n",p_Inp->reffile);

    fprintf(stdout,"--------------------------------------------------------------------------\n");
  #ifdef _LEAKYBUCKET_
    fprintf(stdout," Rate_decoder        : %8ld \n",p_Inp->R_decoder);
    fprintf(stdout," B_decoder           : %8ld \n",p_Inp->B_decoder);
    fprintf(stdout," F_decoder           : %8ld \n",p_Inp->F_decoder);
    fprintf(stdout," LeakyBucketParamFile: %s \n",p_Inp->LeakyBucketParamFile); // Leaky Bucket Param file
    calc_buffer(p_Inp);
    fprintf(stdout,"--------------------------------------------------------------------------\n");
  #endif
  }
  
}

/*********************************************************
if bOutputAllFrames is 1, then output all valid frames to file onetime; 
else output the first valid frame and move the buffer to the end of list;
*********************************************************/
static int WriteOneFrame(DecodedPicList *pDecPic, int hFileOutput0, int hFileOutput1, int bOutputAllFrames)
{
  int iOutputFrame=0;
  DecodedPicList *pPic = pDecPic;

  if(pPic && (((pPic->iYUVStorageFormat==2) && pPic->bValid==3) || ((pPic->iYUVStorageFormat!=2) && pPic->bValid==1)) )
  {
    int i, iWidth, iHeight, iStride, iWidthUV, iHeightUV, iStrideUV;
    byte *pbBuf;    
    int hFileOutput;
    int res;

    iWidth = pPic->iWidth*((pPic->iBitDepth+7)>>3);
    iHeight = pPic->iHeight;
    iStride = pPic->iYBufStride;
    if(pPic->iYUVFormat != YUV444)
      iWidthUV = pPic->iWidth>>1;
    else
      iWidthUV = pPic->iWidth;
    if(pPic->iYUVFormat == YUV420)
      iHeightUV = pPic->iHeight>>1;
    else
      iHeightUV = pPic->iHeight;
    iWidthUV *= ((pPic->iBitDepth+7)>>3);
    iStrideUV = pPic->iUVBufStride;
    
    do
    {
      if(pPic->iYUVStorageFormat==2)
        hFileOutput = (pPic->iViewId&0xffff)? hFileOutput1 : hFileOutput0;
      else
        hFileOutput = hFileOutput0;
      if(hFileOutput >=0)
      {
        //Y;
        pbBuf = pPic->pY;
        for(i=0; i<iHeight; i++)
        {
          res = write(hFileOutput, pbBuf+i*iStride, iWidth);
          if (-1==res)
          {
            error ("error writing to output file.", 600);
          }
        }

        if(pPic->iYUVFormat != YUV400)
        {
         //U;
         pbBuf = pPic->pU;
         for(i=0; i<iHeightUV; i++)
         {
           res = write(hFileOutput, pbBuf+i*iStrideUV, iWidthUV);
           if (-1==res)
           {
             error ("error writing to output file.", 600);
           }
}
         //V;
         pbBuf = pPic->pV;
         for(i=0; i<iHeightUV; i++)
         {
           res = write(hFileOutput, pbBuf+i*iStrideUV, iWidthUV);
           if (-1==res)
           {
             error ("error writing to output file.", 600);
           }
         }
        }

        iOutputFrame++;
      }

      if (pPic->iYUVStorageFormat == 2)
      {
        hFileOutput = ((pPic->iViewId>>16)&0xffff)? hFileOutput1 : hFileOutput0;
        if(hFileOutput>=0)
        {
          int iPicSize =iHeight*iStride;
          //Y;
          pbBuf = pPic->pY+iPicSize;
          for(i=0; i<iHeight; i++)
          {
            res = write(hFileOutput, pbBuf+i*iStride, iWidth);
            if (-1==res)
            {
              error ("error writing to output file.", 600);
            }
          }

          if(pPic->iYUVFormat != YUV400)
          {
           iPicSize = iHeightUV*iStrideUV;
           //U;
           pbBuf = pPic->pU+iPicSize;
           for(i=0; i<iHeightUV; i++)
           {
             res = write(hFileOutput, pbBuf+i*iStrideUV, iWidthUV);
             if (-1==res)
             {
               error ("error writing to output file.", 600);
             }
           }
           //V;
           pbBuf = pPic->pV+iPicSize;
           for(i=0; i<iHeightUV; i++)
           {
             res = write(hFileOutput, pbBuf+i*iStrideUV, iWidthUV);
             if (-1==res)
             {
               error ("error writing to output file.", 600);
             }
           }
          }

          iOutputFrame++;
        }
      }

#if PRINT_OUTPUT_POC
      fprintf(stdout, "\nOutput frame: %d/%d\n", pPic->iPOC, pPic->iViewId);
#endif
      pPic->bValid = 0;
      pPic = pPic->pNext;
    }while(pPic != NULL && pPic->bValid && bOutputAllFrames);
  }
#if PRINT_OUTPUT_POC
  else
    fprintf(stdout, "\nNone frame output\n");
#endif

  return iOutputFrame;
}

void get_KeyFileName(char* path, char* filename)
{
	int len = strlen(path);
	int i;

	for(i = len-1; i >= 0 && path[i] != '/'; --i)
		;

	int j = 0;
	for(i++; i < len; ++i)
	{
		filename[j++] = path[i];
	}

}

void open_KeyFile()
{
	if(!p_Dec->p_Inp->enable_key)
		return;
	
	char key_file[FILE_NAME_SIZE];
	char filename[100];
	
	get_KeyFileName(p_Dec->p_Inp->infile, filename);

	strncpy(key_file, p_Dec->p_Inp->keyfile_dir, strlen(p_Dec->p_Inp->keyfile_dir));
	strncat(key_file, filename, strlen(filename));
	strcat(key_file, ".key");
	//printf("key_file: %s\n",key_file);	

	p_Dec->p_KeyFile = fopen(key_file, "w+");
	if(!p_Dec->p_KeyFile)
	{
		printf("\033[1;31m open key file [%s] error!\033[0m \n",key_file);
		exit(0);
	}
	else
	{
		printf("\033[1;31m open key file [%s] success!\033[0m \n",key_file);
	}
}

void close_KeyFile()
{
	if(p_Dec->p_Inp->enable_key && p_Dec->p_KeyFile)
		fclose(p_Dec->p_KeyFile);
}

KeyUnit* g_pKeyUnitBuffer;
int g_KeyUnitIdx = 0;
int g_KeyUnitBufferSize = 0;

void print_KeyUnit()
{
	FILE* log = fopen("key_unit_log", "w+");
	KeyUnit* p_tmp = g_pKeyUnitBuffer;
	int i = 0;
	char s[100];
	
	for(; i < g_KeyUnitIdx; ++i)
	{		
		snprintf(s,100,"ByteOffset: %5d, BitOffset: %2d, DataLen: %4d\n",
						p_tmp[i].byte_offset,p_tmp[i].bit_offset,p_tmp[i].key_data_len);
		fwrite(s,strlen(s),1,log);	
	}
	printf("i: %d, idx: %d\n",i,g_KeyUnitIdx);
}

void init_GenKeyPar()
{
	if(!p_Dec->p_Inp->enable_key)
		return;
	
	p_Dec->nalu_pos_array = calloc(400,sizeof(int));
	g_pKeyUnitBuffer = (KeyUnit*)malloc(KEY_UNIT_BUFFER_SIZE*sizeof(KeyUnit));
	if(!g_pKeyUnitBuffer)
	{
		printf("\033[1;31m key unit buffer malloc failed!\033[0m \n");
		exit(1);
	}
	g_KeyUnitBufferSize = KEY_UNIT_BUFFER_SIZE;
}
/*!
 ***********************************************************************
 * \brief
 *    main function for JM decoder
 ***********************************************************************
 */
extern int Encrypt(KeyUnit *pKeyUnit,int UnitNum); 
int main(int argc, char **argv)
{
	struct timeval start, end1, end2;
	long int time_us1,time_us2;
	gettimeofday( &start, NULL );
	
  int iRet;
  DecodedPicList *pDecPicList;
  int hFileDecOutput0=-1, hFileDecOutput1=-1;
  int iFramesOutput=0, iFramesDecoded=0;
  InputParameters InputParams;

#if DECOUTPUT_TEST
  hFileDecOutput0 = open(DECOUTPUT_VIEW0_FILENAME, OPENFLAGS_WRITE, OPEN_PERMISSIONS);
  fprintf(stdout, "Decoder output view0: %s\n", DECOUTPUT_VIEW0_FILENAME);
  hFileDecOutput1 = open(DECOUTPUT_VIEW1_FILENAME, OPENFLAGS_WRITE, OPEN_PERMISSIONS);
  fprintf(stdout, "Decoder output view1: %s\n", DECOUTPUT_VIEW1_FILENAME);
#endif

  init_time();

  //get input parameters;
  Configure(&InputParams, argc, argv);
  //open decoder;
  iRet = OpenDecoder(&InputParams);
  if(iRet != DEC_OPEN_NOERR)
  {
    fprintf(stderr, "Open encoder failed: 0x%x!\n", iRet);
    return -1; //failed;
  }

	open_KeyFile();	
	init_GenKeyPar();
	
  //decoding;
  do
  {
    iRet = DecodeOneFrame(&pDecPicList);
    if(iRet==DEC_EOS || iRet==DEC_SUCCEED)
    {
      //process the decoded picture, output or display;
      //iFramesOutput += WriteOneFrame(pDecPicList, hFileDecOutput0, hFileDecOutput1, 0);
      //iFramesDecoded++;
    }
    else
    {
      //error handling;
      fprintf(stderr, "Error in decoding process: 0x%x\n", iRet);
    }
  }while((iRet == DEC_SUCCEED) && ((p_Dec->p_Inp->iDecFrmNum==0) || (iFramesDecoded<p_Dec->p_Inp->iDecFrmNum)));

	gettimeofday( &end1, NULL );
	time_us1 = 1000000 * ( end1.tv_sec - start.tv_sec ) + end1.tv_usec - start.tv_usec;
	printf("run time0: %ld us\n",time_us1);

	//encrypt the H.264 file
	if(p_Dec->p_Inp->enable_key && g_pKeyUnitBuffer && g_KeyUnitIdx > 0)
		Encrypt(g_pKeyUnitBuffer, g_KeyUnitIdx);

	close_KeyFile();
  iRet = FinitDecoder(&pDecPicList);
  //iFramesOutput += WriteOneFrame(pDecPicList, hFileDecOutput0, hFileDecOutput1 , 1);
  iRet = CloseDecoder();

  //quit;
  if(hFileDecOutput0>=0)
  {
    close(hFileDecOutput0);
  }
  if(hFileDecOutput1>=0)
  {
    close(hFileDecOutput1);
  }	

  printf("%d frames are decoded.\n", iFramesDecoded);
	//print_KeyUnit();
	
	gettimeofday( &end2, NULL );
	time_us2 = 1000000 * ( end2.tv_sec - end1.tv_sec ) + end2.tv_usec - end1.tv_usec;
	printf("run time1: %ld us\n",time_us2);
	printf("run time(all): %ld us\n", time_us1+time_us2);
  return 0;
}


