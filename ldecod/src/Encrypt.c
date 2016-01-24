#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include "global.h"

#define MAX_BUFFER_LEN 1024*1024
#define CUT_BIT_LEN 0
#define CUT_BIT_LEN_64 0
#define CUT_BIT_LEN_32 0
#define CUT_BIT_LEN_16 0


#define NOT_CUT_BIT_LEN 1

#define KEY_BIT_LEN_1 6
#define KEY_BIT_LEN_3 3

#if CUT_BIT_LEN_64
#define KEY_BIT_LEN_4 6
#elif CUT_BIT_LEN_32
#define KEY_BIT_LEN_4 5
#elif CUT_BIT_LEN_16
#define KEY_BIT_LEN_4 4
#elif NOT_CUT_BIT_LEN
#define KEY_BIT_LEN_4 8
#endif

#define KEY_MAX_BYTE_LEN 32
typedef struct
{
	uint8_t* start;
	uint8_t* p;
	uint8_t* end;
	int bits_left;
} bs_t;

static inline int bs_eof(bs_t* b) { if (b->p >= b->end) { return 1; } else { return 0; } }

static inline bs_t* bs_init(bs_t* b, uint8_t* buf, size_t size)
{
    b->start = buf;
    b->p = buf;
    b->end = buf + size;
    b->bits_left = 8;
    return b;
}

static inline bs_t* bs_new(uint8_t* buf, size_t size)
{
    bs_t* b = (bs_t*)malloc(sizeof(bs_t));
    bs_init(b, buf, size);
    return b;
}


static inline void bs_skip_u1(bs_t* b)
{    
    b->bits_left--;
    if (b->bits_left == 0) { b->p ++; b->bits_left = 8; }
}

static inline void bs_skip_u(bs_t* b, int n)
{
    int i;
    for ( i = 0; i < n; i++ ) 
    {
        bs_skip_u1( b );
    }
}

static inline void bs_free(bs_t* b)
{
    free(b);
}

static inline uint32_t bs_read_u1(bs_t* b)
{
    uint32_t r = 0;
    
    b->bits_left--;

    if (! bs_eof(b))
    {
        r = ((*(b->p)) >> b->bits_left) & 0x01;
    }

    if (b->bits_left == 0) { b->p ++; b->bits_left = 8; }

    return r;
}
/*读buffer的前n位，结果以十进制u32 return*/
static inline uint32_t bs_read_u(bs_t* b, int n)
{
    uint32_t r = 0;
    int i;
    for (i = 0; i < n; i++)
    {
        r |= ( bs_read_u1(b) << ( n - i - 1 ) );
    }
    return r;
}

/*对指针b指向的字节buffer写入v*/
static inline void bs_write_u1(bs_t* b, uint32_t v)
{
    b->bits_left--;

    if (! bs_eof(b))
    {
        /* FIXME this is slow, but we must clear bit first
         is it better to memset(0) the whole buffer during bs_init() instead? 
         if we don't do either, we introduce pretty nasty bugs*/
        (*(b->p)) &= ~(0x01 << b->bits_left);
        (*(b->p)) |= ((v & 0x01) << b->bits_left);
    }

    if (b->bits_left == 0) { b->p ++; b->bits_left = 8; }
}

/*对指针b指向的字节buffer的前nbit位写入v*/
static inline void bs_write_u(bs_t* b, int n, uint32_t v)
{
    int i;
    for (i = 0; i < n; i++)
    {
        bs_write_u1(b, (v >> ( n - i - 1 ))&0x01 );
    }
}

/*Number需要多少个bit位容纳*/
int GetNeedBitCount(unsigned int Number,int *BitCount )
{
	int i32Count=0;
	
	if(Number<0)
	{
		return -1;
	}

	if(Number==0)
	{
		i32Count=1;
	}
	while(Number!=0)
	{
		i32Count++;
		Number/=2;
	}
    *BitCount=i32Count;
	return 0;
}

int GetKeyByteLen(int ByteOffset,int ByteOffsetBitNum,int BitOffset,int BitLength,int *KeyByteLen)
{
	int KeyBitLength;
	int KeyByteLength;
	
	KeyBitLength=KEY_BIT_LEN_1+ByteOffsetBitNum+KEY_BIT_LEN_3+KEY_BIT_LEN_4+BitLength;
	KeyByteLength=KeyBitLength/8;

	if(KeyBitLength%8!=0)
	{
		KeyByteLength+=1;
	}
	
	*KeyByteLen=KeyByteLength;

	return 0;
}

int bs_Write_KeyData(bs_t *b, int BitLength,uint8_t *s_Keydata)
{
	int Keydata_Byte_Len=BitLength/8;
	int Keydata_RemainBit_Len=BitLength%8;
	int i=0;
	if(Keydata_RemainBit_Len!=0)
	{
		Keydata_Byte_Len++;
	}
	
	for(i=0;i<Keydata_Byte_Len;i++)
	{
		
		if(i==Keydata_Byte_Len-1&&Keydata_RemainBit_Len!=0)
		{
			bs_write_u(b,Keydata_RemainBit_Len,s_Keydata[i]);	
			//printf("data[%d]==0x%x\n",i,s_Keydata[i]);
		}
		else
		{
			//printf("data[%d]==0x%x\n",i,s_Keydata[i]);
			bs_write_u(b,8,s_Keydata[i]);		
		}

	}

	return 0;
}

int bs_Read_KeyData(bs_t *b, int BitLength,uint8_t *s_Keydata)
{
	int Keydata_Byte_Len=BitLength/8;
	int Keydata_RemainBit_Len=BitLength%8;
	int i=0;
	memset(s_Keydata,0,32);
	
	if(Keydata_RemainBit_Len!=0)
	{
		Keydata_Byte_Len++;
	}

	for(i=0;i<Keydata_Byte_Len;i++)
	{
		if(i==Keydata_Byte_Len-1&&Keydata_RemainBit_Len!=0)
		{
			s_Keydata[i]=bs_read_u(b,Keydata_RemainBit_Len);
		}
		else
		{
			s_Keydata[i]=bs_read_u(b,8);
		}
	}
	
	return 0;
}

int Get_Key(int ByteOffset,int BitOffset,int BitLength,uint8_t *s_Keydata,char **key)
{
	uint8_t *u8Buffer;
	int ByteOffsetBitNum=0;
	int KeyByteLength=0;
	bs_t *b;
	
	if(-1 == GetNeedBitCount(ByteOffset,&ByteOffsetBitNum))
	{
		return -1;
	}

	GetKeyByteLen(ByteOffset,ByteOffsetBitNum,BitOffset,BitLength,&KeyByteLength);
	
	u8Buffer=(uint8_t*)malloc(KeyByteLength*sizeof(uint8_t));
	memset(u8Buffer,0x00,KeyByteLength);
	b=bs_new(u8Buffer,KeyByteLength);

	bs_write_u(b,KEY_BIT_LEN_1,ByteOffsetBitNum);
	bs_write_u(b,ByteOffsetBitNum,ByteOffset);
	bs_write_u(b,KEY_BIT_LEN_3,BitOffset);
	bs_write_u(b,KEY_BIT_LEN_4,BitLength);
	bs_Write_KeyData(b,BitLength,s_Keydata);	
	*key=u8Buffer;
	bs_free(b);
	return KeyByteLength;
		
}

int Generate_Key_Get_Changed_ByteNum(int BitLength,int BitOffset,int *ChangedByteNum)
{
	int ByteCount=0;

	ByteCount=(BitOffset+BitLength)/8;
	
	if((BitOffset+BitLength)%8!=0)
	{
		ByteCount+=1;
	}

	*ChangedByteNum=ByteCount;
	return 0;
}

void Encrypt(ThreadUnitPar *thread_unit_par)
{
	int i=0;

	if(p_Dec->p_Inp->multi_thread == 1)
	{	

		for(i=thread_unit_par->buffer_start;i<thread_unit_par->buffer_len;i++)
		{
			Generate_Key(g_pKeyUnitBuffer[i].byte_offset,thread_unit_par->cur_absolute_offset,
											g_pKeyUnitBuffer[i].bit_offset,g_pKeyUnitBuffer[i].key_data_len,0);			
		}

		if(i == thread_unit_par->buffer_len)
			Generate_Key(0,0,0,0,1);
	}
}

int Is_Para_Valid(int RelativeByteOff,int BitOffset,int BitLength)
{
	if(RelativeByteOff<0)
	{
		printf("Param error:RelativeByteOff=(%d)!\n",RelativeByteOff);
		return -1;
	}

	else if(BitOffset<0||BitOffset>=pow(2,KEY_BIT_LEN_3))
	{
		printf("Param error:BitOffset=(%d)!\n",BitOffset);
		return -2;
	}

	else if(BitLength<0)
	{
		printf("Param error:BitLength=(%d)!\n",BitLength);
		return -3;
	}
	 

}

int Generate_Key(int RelativeByteOff,int BitOffset,int BitLength, int canfree)
{

	if(Is_Para_Valid(RelativeByteOff,BitOffset,BitLength)<0)
	{
		return -1;
	}
	
#if CUT_BIT_LEN
	if(BitLength>=pow(2,KEY_BIT_LEN_4))
	{
		BitLength=pow(2,KEY_BIT_LEN_4);
	}
#endif

	int keydata;
	int ChangedByteNum=0;
	static bs_t *b_read,*b_write;
	char *key=NULL;
	static int KeyByteLen;
	static int RelativeByteOff_Sum=0;
	static int BufferStart=0;
	static int read_count=0;
	static int KeyByteLenSum=0;
	
	static char *keyBuffer=NULL;
	static char *h264Buffer=NULL;
	static int lastBitLen=0;
	static int lastBitoffset=0;
	static int LastByteOffset=0;
	static int ByteOffset=0;
	int tmpRelativeByteOff=0;
	LastByteOffset=ByteOffset;
	ByteOffset+=RelativeByteOff;
	tmpRelativeByteOff=RelativeByteOff;
	Generate_Key_Get_Changed_ByteNum(BitLength,BitOffset,&ChangedByteNum);
	
	
	if(LastByteOffset==0)
	{
		//if(p_Dec->p_Inp->multi_thread == 1)
			//ByteOffset=cur_absolute_offset;
		
		lseek(p_Dec->BitStreamFile,ByteOffset,SEEK_SET);
		BufferStart=ByteOffset;

		h264Buffer=(char *)malloc(MAX_BUFFER_LEN*sizeof(char));
		memset(h264Buffer,0x00,MAX_BUFFER_LEN);
	
		read_count=read(p_Dec->BitStreamFile,h264Buffer,MAX_BUFFER_LEN);

		if(0==read_count)
		{
			return -1;
		}

		b_read=bs_new(h264Buffer,MAX_BUFFER_LEN);
		b_write=bs_new(h264Buffer,MAX_BUFFER_LEN);

		keyBuffer=(char *)malloc(MAX_BUFFER_LEN*sizeof(char));
		memset(keyBuffer,0x00,MAX_BUFFER_LEN);
	}
	else if(LastByteOffset>0)
	{	
		RelativeByteOff_Sum+=RelativeByteOff;

		if(RelativeByteOff_Sum+ChangedByteNum<MAX_BUFFER_LEN)
		{	
			if(RelativeByteOff*8-lastBitoffset-lastBitLen>=0)
			{	
				bs_skip_u(b_read,RelativeByteOff*8-lastBitoffset-lastBitLen);
				bs_skip_u(b_write,RelativeByteOff*8-lastBitoffset-lastBitLen);	
			}	
		}		
		else
		{
			lseek(p_Dec->BitStreamFile,BufferStart,SEEK_SET);
			write(p_Dec->BitStreamFile,h264Buffer,MAX_BUFFER_LEN);

			lseek(p_Dec->BitStreamFile,ByteOffset,SEEK_SET);
			BufferStart=ByteOffset;
			read_count=read(p_Dec->BitStreamFile,h264Buffer,MAX_BUFFER_LEN);

			if(0==read_count)
			{
				return -1;
			}
			
			b_read=bs_new(h264Buffer,MAX_BUFFER_LEN);
			b_write=bs_new(h264Buffer,MAX_BUFFER_LEN);
			RelativeByteOff_Sum=0;
			tmpRelativeByteOff=0;
			lastBitoffset=0;
			lastBitLen=0;
		}
	}

	
	if(canfree)
	{
		lseek(p_Dec->BitStreamFile,BufferStart,SEEK_SET);
		write(p_Dec->BitStreamFile,h264Buffer,read_count);
		fwrite(keyBuffer,sizeof(char),KeyByteLenSum,p_Dec->p_KeyFile);
		/*write 0x00 to keyfile as end of file*/
		fputc(0x00,p_Dec->p_KeyFile);		
		free(key);
		free(keyBuffer);
		free(h264Buffer);
		free(b_read);
		free(b_write);
		return 0;
	}
	

	if(tmpRelativeByteOff*8-lastBitoffset-lastBitLen>=0)
	{
		bs_skip_u(b_read,BitOffset);
		bs_skip_u(b_write,BitOffset);		
	}
	else
	{
	    bs_skip_u(b_read,BitOffset-(lastBitoffset+lastBitLen)%8);
		bs_skip_u(b_write,BitOffset-(lastBitoffset+lastBitLen)%8);	
	}

	uint8_t s_Keydata[32]={0x00};
	int Keydata_Byte_Len=BitLength/8;
	int Keydata_RemainBit_Len=BitLength%8;
	int i=0;
	if(Keydata_RemainBit_Len!=0)
	{
		Keydata_Byte_Len++;
	}

	for(i=0;i<Keydata_Byte_Len;i++)
	{
		if(i==Keydata_Byte_Len-1 && Keydata_RemainBit_Len!=0)
		{
			s_Keydata[i]=bs_read_u(b_read,Keydata_RemainBit_Len);
			bs_write_u(b_write,Keydata_RemainBit_Len,0);	
		}
		else
		{
			s_Keydata[i]=bs_read_u(b_read,8);
			bs_write_u(b_write,8,0);
		}
		
	}

	lastBitLen=BitLength;
	lastBitoffset=BitOffset;
	
	KeyByteLen=Get_Key(RelativeByteOff,BitOffset,BitLength,s_Keydata,&key);
	KeyByteLenSum+=KeyByteLen;

	if(KeyByteLenSum<=MAX_BUFFER_LEN)
	{
		memcpy(keyBuffer+KeyByteLenSum-KeyByteLen,key,KeyByteLen);
	}
	else
	{
		fwrite(keyBuffer,sizeof(char),KeyByteLenSum-KeyByteLen,p_Dec->p_KeyFile);
		memset(keyBuffer,0x00,MAX_BUFFER_LEN);

		memcpy(keyBuffer,key,KeyByteLen);
		KeyByteLenSum=KeyByteLen;
	}
	
	return 0;		
}

