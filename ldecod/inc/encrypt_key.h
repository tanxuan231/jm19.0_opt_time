#ifndef _ENCRYPT_KEY_H_
#define _ENCRYPT_KEY_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef unsigned char uint8_t;

/*
*	start            end
*		|               |
*		---------------
*	 |   |   |   |   |
*		---------------
*       |
*       p
*/

#define FAST_U8 

typedef struct
{
	uint8_t* start;		//buf的起始
	uint8_t* p;				//指向正在处理的字节
	uint8_t* end;			//buf的结束后一个字节处
	int bits_left;		//正在处理字节剩余的位数
} bs_t;

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

static inline int bs_eof(bs_t* b) 
{ 
	if (b->p >= b->end) 
	{ 
		return 1; 
	} 
	else 
	{ 
		return 0; 
	} 
}

//将v中最低位写入b中
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

//将v中的后n位写入b
// 1100 0111 当n=5时，写入到b中的为0011 1
static inline void bs_write_u(bs_t* b, int n, uint32_t v)
{
    int i;
    for (i = 0; i < n; i++)
    {
        bs_write_u1(b, (v >> ( n - i - 1 ))&0x01 );
    }
}

//将v中的起始n位写入b中,n:1~7
// 1100 0111 当n=5时，写入到b中的为1100 0
static inline void bs_write_c(bs_t* b, int n, uint8_t v)
{
	int i;
	for(i = 0; i < n; i++)
	{
		bs_write_u1(b, ((v&(0x1<<(7-i))) == 0 ? 0:1));
	}
}

static inline void bs_write_u8(bs_t* b, uint32_t v)
{
#ifdef FAST_U8
    if (b->bits_left == 8 && ! bs_eof(b)) // can do fast write
    {
        b->p[0] = v;
        b->p++;
        return;
    }
#endif
    bs_write_u(b, 8, v);
}

#endif