
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

//#include "global.h"
#include "win32.h"
#include "h264decoder.h"
#include "configfile.h"

#define PRINT_OUTPUT_POC    0
#define BITSTREAM_FILENAME  "test.264"
//#define DECRECON_FILENAME   "test_dec.yuv"
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
  //strcpy(p_Inp->outfile, DECRECON_FILENAME); //! set default output file name
  //strcpy(p_Inp->reffile, ENCRECON_FILENAME); //! set default reference file name
  
  ParseCommand(p_Inp, ac, av);

  fprintf(stdout,"----------------------------- JM %s %s -----------------------------\n", VERSION, EXT_VERSION);
  //fprintf(stdout," Decoder config file                    : %s \n",config_filename);
  if(!p_Inp->bDisplayDecParams)
  {
    fprintf(stdout,"--------------------------------------------------------------------------\n");
    fprintf(stdout," Input H.264 bitstream                  : %s \n",p_Inp->infile);
    //fprintf(stdout," Output decoded YUV                     : %s \n",p_Inp->outfile);
    //fprintf(stdout," Output status file                     : %s \n",LOGFILE);
    //fprintf(stdout," Input reference file                   : %s \n",p_Inp->reffile);

    fprintf(stdout,"--------------------------------------------------------------------------\n");
  }
  
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
	strcat(key_file, ".txt");
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

int g_KeyUnitIdx = 0;
int g_KeyUnitBufferSize = 0;

void print_KeyUnit()
{
	FILE* log = fopen("key_unit_log", "w+");
	if(!log)
	{
		printf("open key_unit_log error!\n");
		exit(1);
	}
	KeyUnit* p_tmp = g_pKeyUnitBuffer;
	int i = 0;
	char s[100];
	int pre_off = 0;
	
	for(; i < g_KeyUnitIdx; ++i)
	{		
		//pre_off += p_tmp[i].byte_offset;
		snprintf(s,100,"ByteOffset: %5d, BitOffset: %2d, DataLen: %4d\n",
						p_tmp[i].byte_offset,p_tmp[i].bit_offset,p_tmp[i].key_data_len);
		fwrite(s,strlen(s),1,log);		
	}
	printf("KeyUnitIdx: %d\n",i,g_KeyUnitIdx);
}

void init_GenKeyPar()
{
	if(!p_Dec->p_Inp->enable_key)
		return;

	open_KeyFile();	
	p_Dec->nalu_pos_array = calloc(NALU_NUM_IN_BITSTREAM,sizeof(int));
	if(!p_Dec->nalu_pos_array)
	{
		printf("\033[1;31m p_Dec->nalu_pos_array malloc failed!\033[0m \n");
		exit(1);
	}
		
	g_pKeyUnitBuffer = (KeyUnit*)malloc(KEY_UNIT_BUFFER_SIZE*sizeof(KeyUnit));
	if(!g_pKeyUnitBuffer)
	{
		printf("\033[1;31m key unit buffer malloc failed!\033[0m \n");
		exit(1);
	}
	g_KeyUnitBufferSize = KEY_UNIT_BUFFER_SIZE;

	/*********use multi thread********/
	if(p_Dec->p_Inp->multi_thread == 1)
	{
		int ret;
			
		ret = pthread_attr_init(&p_Dec->thread_attr);
		if (ret != 0)
		{
			printf("ptread_atrr_init error!\n");
			exit(1);
		}
	}
}

void deinit_GenKeyPar()
{
	if(!p_Dec->p_Inp->enable_key)
		return;
	
	free(p_Dec->nalu_pos_array);		
	free(g_pKeyUnitBuffer);

	if(p_Dec->p_KeyFile)
		fclose(p_Dec->p_KeyFile);
}	
extern int g_ThreadParCurPos;
extern int g_KeyUnitBufferLen;
/*!
 ***********************************************************************
 * \brief
 *    main function for JM decoder
 ***********************************************************************
 */
extern int Encrypt(int UnitNum); 
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
	printf("run time0: %ld us\n",time_us1);

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
		par->cur_absolute_offset = 0;
		Encrypt(par);
	}
	
	gettimeofday( &end2, NULL );
	time_us2 = 1000000 * ( end2.tv_sec - end1.tv_sec ) + end2.tv_usec - end1.tv_usec;
	printf("run time1: %ld us\n",time_us2);

	//print_KeyUnit();

	deinit_GenKeyPar();
  iRet = FinitDecoder();
  iRet = CloseDecoder();	
	
	printf("run time(all): %ld us\n", time_us1+time_us2);
  return 0;
}


