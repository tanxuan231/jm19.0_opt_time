#include <stdio.h>
#include <string.h>

#include "global.h"
#include "key_common.h"

int g_KeyUnitIdx = 0;
int g_KeyUnitBufferSize = 0;

static void change_char(char *a, char *b)
{
	char tmp = *b;
	*b = *a;
	*a = tmp;
}

static void get_KeyFileName(const char* path, char* filename)
{
	int len = strlen(path);
	int i, j = 0,k;

	for(i = len-1; i >= 0 && path[i] != '/'; --i)
	{
		filename[j++] = path[i];
	}

	for(i = 0, k = j-1; i < k; ++i,--k)
	{
		change_char(&filename[i], &filename[k]);
	}
	filename[j] = '\0';
}

void open_KeyFile()
{
	if(!p_Dec->p_Inp->enable_key)
		return;
	
	char key_file[FILE_NAME_SIZE] = {0};
	char filename[255] = {0};
	
	get_KeyFileName(p_Dec->p_Inp->infile, filename);

	strncpy(key_file, p_Dec->p_Inp->keyfile_dir, strlen(p_Dec->p_Inp->keyfile_dir));
	strncat(key_file, filename, strlen(filename));
	strcat(key_file, ".key");
	strcat(key_file, ".txt");
	printf("key_file: %s\n",key_file);	

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

static int ku_inrange(int data, int a, int b)
{
	if(data >= a && data <= b)
		return 1;
	else
		return 0;
}

int ku_byteoffset_count[13] = {0};
int ku_datalen_count[13] = {0};

static void Boffset_distribute(int data)
{
	if(ku_inrange(data, 0, 1))
		ku_byteoffset_count[0]++;
	else if(ku_inrange(data, 2, 3))
		ku_byteoffset_count[1]++;
	else if(ku_inrange(data, 4, 7))
		ku_byteoffset_count[2]++;
	else if(ku_inrange(data, 8, 15))
		ku_byteoffset_count[3]++;
	else if(ku_inrange(data, 16, 31))
		ku_byteoffset_count[4]++;
	else if(ku_inrange(data, 32, 63))
		ku_byteoffset_count[5]++;
	else if(ku_inrange(data, 64, 127))
		ku_byteoffset_count[6]++;
	else if(ku_inrange(data, 128, 255))
		ku_byteoffset_count[7]++;
	else if(ku_inrange(data, 256, 511))
		ku_byteoffset_count[8]++;
	else if(ku_inrange(data, 512, 1023))
		ku_byteoffset_count[9]++;
	else if(ku_inrange(data, 1024, 2045))
		ku_byteoffset_count[10]++;
	else if(ku_inrange(data, 2046, 4095))
		ku_byteoffset_count[11]++;
	else
		ku_byteoffset_count[12]++;

}

static void DataLen_distribute(int data)
{
	if(ku_inrange(data, 0, 1))
		ku_datalen_count[0]++;
	else if(ku_inrange(data, 2, 3))
		ku_datalen_count[1]++;
	else if(ku_inrange(data, 4, 7))
		ku_datalen_count[2]++;
	else if(ku_inrange(data, 8, 15))
		ku_datalen_count[3]++;
	else if(ku_inrange(data, 16, 31))
		ku_datalen_count[4]++;
	else if(ku_inrange(data, 32, 63))
		ku_datalen_count[5]++;
	else if(ku_inrange(data, 64, 127))
		ku_datalen_count[6]++;
	else if(ku_inrange(data, 128, 255))
		ku_datalen_count[7]++;
	else if(ku_inrange(data, 256, 511))
		ku_datalen_count[8]++;
	else if(ku_inrange(data, 512, 1023))
		ku_datalen_count[9]++;
	else if(ku_inrange(data, 1024, 2045))
		ku_datalen_count[10]++;
	else if(ku_inrange(data, 2046, 4095))
		ku_datalen_count[11]++;
	else
		ku_datalen_count[12]++;

}


void print_KeyUnit()
{
	FILE* log = fopen("key_unit_log", "w+");
	if(!log)
	{
		printf("open key_unit_log error!\n");
		exit(1);
	}

	FILE* dist = fopen("key_unit_distribute", "a+");
	if(!dist)
	{
		printf("open key_unit_distribute error!\n");
		exit(1);
	}
	
	KeyUnit* p_tmp = g_pKeyUnitBuffer;
	int i = 0;
	char s[255];
	int pre_off = 0;

	char filename[255];
	get_KeyFileName(p_Dec->p_Inp->infile, filename);

	snprintf(s,255,"*************************************************************************\n");
	fwrite(s,strlen(s),1,dist);
	snprintf(s,255,"filename: %s, size: %d entropy mode: %d [0:cavlc, 1:cabac]\n",
					filename,p_Dec->BitStreamFileLen,p_Dec->p_Vid->active_pps->entropy_coding_mode_flag);
	fwrite(s,strlen(s),1,dist);

	int dsum = g_KeyUnitIdx;
	for(; i < g_KeyUnitIdx; ++i)
	{		
		pre_off += p_tmp[i].byte_offset;
		//snprintf(s,255,"Boffset: %5d, ByteOffset: %5d, BitOffset: %2d, DataLen: %4d\n",
						//pre_off,p_tmp[i].byte_offset,p_tmp[i].bit_offset,p_tmp[i].key_data_len);
		snprintf(s,255,"ByteOffset: %5d, BitOffset: %2d, DataLen: %4d\n",
						p_tmp[i].byte_offset,p_tmp[i].bit_offset,p_tmp[i].key_data_len);
		Boffset_distribute(p_tmp[i].byte_offset);
		DataLen_distribute(p_tmp[i].key_data_len);
		fwrite(s,strlen(s),1,log);		
	}

	//key unit数据统计
	int sum = g_KeyUnitIdx;
	int* bcnt = ku_byteoffset_count;
	snprintf(s,255,"byte offset:\n");
	fwrite(s,strlen(s),1,dist);
	snprintf(s,255,"01:%8d %5.3f, 02:%8d %5.3f, 03:%8d %5.3f, 04:%8d %5.3f, 05:%8d %5.3f \n",
					bcnt[0],(float)bcnt[0]/sum,bcnt[1],(float)bcnt[1]/sum,bcnt[2],(float)bcnt[2]/sum,bcnt[3],(float)bcnt[3]/sum,bcnt[4],(float)bcnt[4]/sum);
	fwrite(s,strlen(s),1,dist);
	snprintf(s,255,"06:%8d %5.3f, 07:%8d %5.3f, 08:%8d %5.3f, 09:%8d %5.3f, 10:%8d %5.3f \n",
					bcnt[5],(float)bcnt[5]/sum, bcnt[6],(float)bcnt[6]/sum, bcnt[7],(float)bcnt[7]/sum, bcnt[8],(float)bcnt[8]/sum, bcnt[9],(float)bcnt[9]/sum);
	fwrite(s,strlen(s),1,dist);		
	snprintf(s,255,"11:%8d %5.3f, 12:%8d %5.3f, 13:%8d %5.3f \n",
					bcnt[10],(float)bcnt[10]/sum, bcnt[11],(float)bcnt[11]/sum, bcnt[12],(float)bcnt[12]/sum);
	fwrite(s,strlen(s),1,dist);		

	int* bitc = ku_datalen_count;
	snprintf(s,255,"data len:\n");
	fwrite(s,strlen(s),1,dist);
	snprintf(s,255,"01:%8d %5.3f, 02:%8d %5.3f, 03:%8d %5.3f, 04:%8d %5.3f, 05:%8d %5.3f\n",
					bitc[0],(float)bitc[0]/dsum,bitc[1],(float)bitc[1]/dsum,bitc[2],(float)bitc[2]/dsum,bitc[3],(float)bitc[3]/dsum,bitc[4],(float)bitc[4]/dsum);
	fwrite(s,strlen(s),1,dist);
	snprintf(s,255,"06:%8d %5.3f, 07:%8d %5.3f, 08:%8d %5.3f, 09:%8d %5.3f, 10:%8d %5.3f\n",
					bitc[5],(float)bitc[5]/dsum,bitc[6],(float)bitc[6]/dsum,bitc[7],(float)bitc[7]/dsum,bitc[8],(float)bitc[8]/dsum,bitc[9],(float)bitc[9]/dsum);
	fwrite(s,strlen(s),1,dist);		
	snprintf(s,255,"11:%8d %5.3f, 12:%8d %5.3f, 13:%8d %5.3f\n",
					bitc[10],(float)bitc[10]/dsum,bitc[11],(float)bitc[11]/dsum,bitc[12],(float)bitc[12]/dsum);
	fwrite(s,strlen(s),1,dist);			

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

