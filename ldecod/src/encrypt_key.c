#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "global.h"

/************************************************************************************/
#define MAX_BUF_SIZE 1024*1024*50

char hash_high[9] = 
{
	0x0,
	0x80, // 1000 0000
	0xc0,	// 1100 0000	
	0xe0,	// 1110 0000	
	0xf0,	// 1111 0000	
	0xf8,	// 1111 1000	
	0xfc,	// 1111 1100	
	0xfe,	// 1111 1110	
	0xff	// 1111 1111	
};
char hash_low[9] 	= 
{
	0x00,	// 0000 0000
	0x01,	// 0000 0001
	0x03,	// 0000 0011
	0x07,	// 0000 0111
	0x0f,	// 0000 1111
	0x1f, // 0001 1111
	0x3f,	// 0011 1111
	0x7f,	// 0111 1111
	0xff	// 1111 1111
};

char hash_key_high[9] =
{
	0xff, // 1111 1111
	0x7f, // 0111 1111
	0x3f, // 0011 1111
	0x1f, // 0001 1111
	0x0f, // 0000 1111
	0x07, // 0000 0111
	0x03, // 0000 0011
	0x01, // 0000 0001
	0x0,	// 0000 0000
};
//key unit format
/*
*	byte offset flag	| byte offset len
*	00: 3 bit					|	0~7	
* 01: 4 bit					| 8~15
* 10: 5 bit					| 16~31
*	11: 4 bits + x		| 32~32767
*			use 4 bits to decide the length(could reach 0~15bits and the length could reach 0~32767) of byte offset 
*/
char ByteOffsetFlag[4] =
{
	0x00, 0x01, 0x10, 0x11
};
#define BOFFSET_FLAG4_MASK 0xb0 // 1100 0000
#define BOFFSET_FLAG_LEN 2
#define BOFFSET_FLAG4 4	// fixed:4 bits
#define BOFFSET_FLAG_LEN 31

unsigned int count_bits(unsigned int n)
{
	int i = 0;
	while(n)
	{
		n >>= 1;
		i++;
	}

	return i;
}

void produce_key_one_unit(unsigned int byteoffset)
{
	if(byteoffset > BOFFSET_FLAG_LEN)
	{
		//byte offset flag is: 0x11
		unsigned int len = count_bits(byteoffset);
		unsigned int data = (len << BOFFSET_FLAG_LEN) | BOFFSET_FLAG4_MASK;

		data >>= 2; //È¥µôÄ©Î²µÄ2¸ö0
		
		int uint_size = 8*sizeof(unsigned int);
		int cnt = uint_size - len;
		while(cnt)
			byteoffset <<= 1;
		
	}
}

static inline void* en_malloc(size_t size)
{
	void* tmp = malloc(size);
	if(!tmp)
	{
		printf("en_malloc error!\n");
		exit(1);
	}
}

static void KU_copy(KeyUnit* dest, KeyUnit* src)
{
	dest->bit_offset = src->bit_offset;
	dest->byte_offset = src->byte_offset;
	dest->key_data_len = src->key_data_len;
}

// buf_264_start is the start of dealing with one unit int h264 bit stream
static void encryt_one_unit(char* buf_key, int buf_key_len, char* buf_264, int buf_264_start, KeyUnit* KUBuf, int KUBuf_idx)
{
	int i;

	int byteoffset 	= KUBuf[KUBuf_idx].byte_offset;
	int bitoffset 	= KUBuf[KUBuf_idx].bit_offset;
	int datalen			= KUBuf[KUBuf_idx].key_data_len;
	int bit_sum 		= bitoffset + datalen;
	int read_byte 	= 0;

	//counting the bytes should be read to deal with
	read_byte = bit_sum/8;
	if(bit_sum%8)
		read_byte++;

	#if 0
	printf("read_byte: %d\n",read_byte);

	printf("print the data before change:\n");	
	for(i=buf_264_start; i<buf_264_start + read_byte; ++i)
		printf("0x%x ",buf_264[i]);
	printf("\n");
	#endif

	int first_byte_mask = 0;	
	int last_byte_mask = 0;

	if(bit_sum > 8)	//over than one byte
	{
		first_byte_mask = bitoffset;
		last_byte_mask  = read_byte*8 - bit_sum;

		//deal with the first byte
		buf_264[buf_264_start] &= hash_high[first_byte_mask];

		//deal with the other bytes
		i = buf_264_start + 1;
		for(;i < buf_264_start + read_byte -1; ++i)
			buf_264[i] &= 0x0;

		//deal with the last byte
		buf_264[buf_264_start + read_byte - 1] &= hash_low[last_byte_mask];
	}
	else if(bit_sum == 8)	//only one byte
	{
		first_byte_mask = bitoffset;
		buf_264[buf_264_start] &= hash_high[first_byte_mask];
	}
	else  //litter then a byte
	{
		first_byte_mask = bitoffset;
		last_byte_mask  = read_byte*8 - bit_sum;

		char tmp = hash_high[first_byte_mask] ^ hash_low[last_byte_mask];
		buf_264[buf_264_start] &= tmp;
	}

	#if 0
	printf("print the data after change:\n");
	for(i=buf_264_start; i<buf_264_start + read_byte; ++i)
		printf("0x%x ",buf_264[i]);
	printf("\n");
	#endif
}

void encryt_thread(ThreadUnitPar* thread_unit_par)
{	
	int i = 0, j = 0;		
	int rd_cnt;
	
	char* buf_264;
	buf_264 = (char*)malloc(sizeof(char)*MAX_BUF_SIZE);
	if(!buf_264)
	{
		printf("encryt_thread: malloc error!\n");
		exit(1);
	}
			
	KeyUnit* KUBuf;
	KUBuf = (KeyUnit*)malloc(sizeof(KeyUnit)*thread_unit_par->buffer_len);
	if(!KUBuf)
	{
		printf("encryt_thread: malloc error!\n");
		exit(1);
	}
	
	int fd = p_Dec->BitStreamFile;//open("bus_cavlc_Copy.264",O_RDWR);

	j = 0;
	for(i = thread_unit_par->buffer_start; i < thread_unit_par->buffer_len; i++)	// should locked g_pKeyUnitBuffer
	{
		KU_copy(&KUBuf[j],&g_pKeyUnitBuffer[i]);
		j ++;
	}
	
	lseek(fd, thread_unit_par->cur_absolute_offset, SEEK_SET);	// should locked fd
	rd_cnt = read(fd, buf_264, MAX_BUF_SIZE);	// should locked fd

	int start = 0;
	KUBuf[0].byte_offset = 0;
	/*** encryt every key unit***/
	for(i = thread_unit_par->buffer_start; i < thread_unit_par->buffer_len; i++)
	{
		start += KUBuf[i].byte_offset;
		if(start > rd_cnt)
		{
			printf("buf is too litter!\n");
		}
		//encryt_one_unit(buf_264, start, KUBuf, i);
	}

	int wr_cnt;
	lseek(fd, thread_unit_par->cur_absolute_offset, SEEK_SET);
	wr_cnt = write(fd, buf_264, rd_cnt);	// should locked fd
	if(wr_cnt == -1)
	{
		printf("write to 264 bs error!\n");
	}
	else if(wr_cnt != rd_cnt)
	{
		printf("write to 264 bs file litter than the cnt: %d < %d\n",wr_cnt, rd_cnt);
	}

	free(KUBuf);
}
/************************************************************************************/

