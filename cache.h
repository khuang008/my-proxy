/************************************************************
	cache.h
	A cache implementation for proxy lab
	Name: Kaimin Huang
	Andrew ID: kaiminh1

************************************************************/

#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

/*
	The cache block structure
*/
struct cache_struc {
    char*  url; // url for idendify the request
    char*  response; // store the response from server
    unsigned long time_stamp; //record the time information
    size_t response_size; // record the size of the response(number of bytes)
    struct cache_struc* next; // pointer to the next cache block
};
typedef struct cache_struc  Cache_t;


/* declare functions for cache operation */
Cache_t* construct_cache_block(char*  url, char* response,
	size_t response_size);
Cache_t* find_in_cache( char* url,Cache_t* cache);
int add_to_cache(Cache_t *p,Cache_t** cache);
int evict_cache(size_t * total_cache_size,Cache_t** cache);
void free_cache_block(Cache_t* cache_block);
void free_cache(Cache_t* cache);
void print_cache(size_t total_cache_size,Cache_t* cache);
void update_time_stamp(Cache_t* hit_cache,Cache_t* cache);

#endif /* __CACHE_H__ */
