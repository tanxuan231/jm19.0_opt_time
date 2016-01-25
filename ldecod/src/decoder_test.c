
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
#include <pthread.h> 

#include "win32.h"
#include "h264decoder.h"
#include "configfile.h"
#include "key_common.h"


extern int g_ThreadParCurPos;
extern int g_KeyUnitBufferLen;
extern void Encrypt(ThreadUnitPar *thread_unit_par); 
extern void encryt_thread(ThreadUnitPar* thread_unit_par);

static void Configure(InputParameters *p_Inp, int ac, char *av[])
{
  memset(p_Inp, 0, sizeof(InputParameters));
  
  ParseCommand(p_Inp, ac, av);

  fprintf(stdout,"----------------------------- JM %s %s -----------------------------\n", VERSION, EXT_VERSION);
  if(!p_Inp->bDisplayDecParams)
  {
    fprintf(stdout,"--------------------------------------------------------------------------\n");
    fprintf(stdout," Input H.264 bitstream                  : %s \n",p_Inp->infile);
    fprintf(stdout,"--------------------------------------------------------------------------\n");
  }
  
}

/*!
 ***********************************************************************
 * \brief
 *    main function for JM decoder
 ***********************************************************************
 */

int main(int argc, char **argv)
{
	struct timeval start, end1, end2;
	long int time_us1,time_us2;
	gettimeofday( &start, NULL );
	
  int iRet;
  InputParameters InputParams;
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

	init_GenKeyPar();
	
  //decoding;
  do
  {
    iRet = DecodeOneFrame();
    if(iRet==DEC_EOS || iRet==DEC_SUCCEED)
    {

    }
    else
    {
      //error handling;
      fprintf(stderr, "Error in decoding process: 0x%x\n", iRet);
    }
  }while((iRet == DEC_SUCCEED) /*&& ((p_Dec->p_Inp->iDecFrmNum==0) || (iFramesDecoded<p_Dec->p_Inp->iDecFrmNum))*/);

	gettimeofday( &end1, NULL );
	time_us1 = 1000000 * ( end1.tv_sec - start.tv_sec ) + end1.tv_usec - start.tv_usec;
	printf("\nrun time0: %ld us\n",time_us1);

	//encrypt the H.264 file
	printf("key unit count: %d\n",g_KeyUnitIdx);

	int ret;
	void* status;
	pthread_attr_t attr;

	/*********use multi thread********/
	if(p_Dec->p_Inp->multi_thread == 1)
	{
		pthread_attr_t attr;
						
		ret = pthread_attr_destroy(&p_Dec->thread_attr);
		if (ret != 0)
		{
			printf("pthread_attr_destroy error: %s\n",strerror(ret));
		}
	}

	ThreadUnitPar* par = NULL;
	par = (ThreadUnitPar*)malloc(sizeof(ThreadUnitPar));
	if(!par)
	{
		printf("malloc failed!\n");
		exit(1);
	}
	memset(par,0,sizeof(ThreadUnitPar));
	
	if(p_Dec->p_Inp->multi_thread)  
	{
		//deal with the rest KU buffer data
		if(g_KeyUnitBufferLen <= MAX_THREAD_DO_KEY_UNIT_CNT)
		{
			par->buffer_start = g_KeyUnitIdx - g_KeyUnitBufferLen;
			par->buffer_len = g_KeyUnitBufferLen;
			par->cur_absolute_offset = g_ThreadParCurPos;
			Encrypt(par);
		}

		int i;
		for(i = 0; i < p_Dec->pid_id; ++i)
		{
			pthread_join(p_Dec->pid[i], &status);
		}
	}
	else
	{		
		par->buffer_start = 0;
		par->buffer_len = g_KeyUnitIdx;
		par->cur_absolute_offset = g_pKeyUnitBuffer[0].byte_offset;
		Encrypt(par);
		//encryt_thread(par);
	}

	gettimeofday( &end2, NULL );
	time_us2 = 1000000 * ( end2.tv_sec - end1.tv_sec ) + end2.tv_usec - end1.tv_usec;
	printf("run time1: %ld us\n",time_us2);

	//print_KeyUnit();

	deinit_GenKeyPar();
  iRet = FinitDecoder();
  iRet = CloseDecoder();	

	fflush(NULL);
	printf("run time(all): %ld us\n", time_us1+time_us2);
  return 0;
}


