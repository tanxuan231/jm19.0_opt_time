#include "stdio.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include<fcntl.h>

/*将x的第y位设为0*/
#define clrbit(x,y) x&=~(1<<y)

/*将x的第y位设为1*/
#define setbit(x,y) x|=(1<<y)


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

int Write_KeyFile(uint32_t ByteOffset,uint32_t BitOffset,uint32_t BitLength,uint32_t data,FILE *KeyFile)
{
	uint8_t *u8Buffer;
	uint32_t u32ByteOffsetBitNum=0;
	uint32_t u32ByteOffsetByteNum=0;	
	
	bs_t *b;
	uint8_t binary[30]={0x00};
	int i=0;
	/*KeyByteLength是key需要多少个字节*/
	size_t KeyByteLength;
	
	if(-1 == GetNeedBitCount(ByteOffset,&u32ByteOffsetBitNum))
	{
		return -1;
	}
	int keyBitLen[5]={8,u32ByteOffsetBitNum,3,5,BitLength};
	
	KeyByteLength=(8+u32ByteOffsetBitNum+3+5+BitLength)/8;

	if((8+u32ByteOffsetBitNum+3+5+BitLength)%8!=0)
	{
		KeyByteLength+=1;
	}
	
	u8Buffer=(uint8_t*)malloc(KeyByteLength*sizeof(uint8_t));
	/*将key缓冲区全置为0*/
	memset(u8Buffer,0x00,KeyByteLength);
	b=bs_new(u8Buffer,KeyByteLength);

	bs_write_u(b,keyBitLen[0],u32ByteOffsetBitNum);

	bs_write_u(b,keyBitLen[1],ByteOffset);
	bs_write_u(b,keyBitLen[2],BitOffset);
	bs_write_u(b,keyBitLen[3],BitLength);
	bs_write_u(b,keyBitLen[4],data);

	while(i<KeyByteLength)
	{
		fputc(u8Buffer[i],KeyFile);
		i++;
	}
	
	free(u8Buffer);
	bs_free(b);
	return 1;
	
}

int Generate_Key(int LastByteOffset,int ByteOffset,int BitOffset,int BitLength,FILE* KeyFile,int h264fd)
{
	uint32_t keydata;
	int ByteCount=0;
	bs_t *b_read,*b_write;
	uint8_t *buffer;
	int ByteOffsetDiffer=ByteOffset-LastByteOffset;
	
	ByteCount=(BitOffset+BitLength)/8;
	
	if((BitOffset+BitLength)%8!=0)
	{
		ByteCount+=1;
	}
	
	buffer=(uint8_t *)malloc(ByteCount*sizeof(uint8_t));
	memset(buffer,0x00,ByteCount);
	lseek(h264fd,ByteOffset,SEEK_SET);
	read(h264fd,buffer,ByteCount);
	b_read=bs_new(buffer,ByteCount);
	bs_skip_u(b_read,BitOffset);
	
	b_write=bs_new(buffer,ByteCount);
	bs_skip_u(b_write,BitOffset);
	
	keydata=bs_read_u(b_read,BitLength);
	bs_write_u(b_write,BitLength,0x00);

	lseek(h264fd,ByteOffset,SEEK_SET);
	write(h264fd,buffer,ByteCount);

	bs_free(b_read);
	bs_free(b_write);
	free(buffer);
	Write_KeyFile(ByteOffsetDiffer,BitOffset,BitLength,keydata,KeyFile);
	
	return 1;
}




