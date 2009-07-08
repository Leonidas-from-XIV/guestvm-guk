#ifndef _STDLIB_H_
#define _STDLIB_H_

static inline int abs(int a)
{
	return a<0?-a:a;
}


static inline long labs(long a)
{
	return a<0?-a:a;
}

static inline long long labs(long long a)
{
	return a<0?-a:a;
}
#endif
