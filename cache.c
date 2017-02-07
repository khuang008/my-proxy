/************************************************************
	cache.c
	A cache implementation for proxy lab
	Name: Kaimin Huang
	Andrew ID: kaiminh1

************************************************************/
#include "cache.h"

/*
	construct_cache_block:
		construct a new cache block and set the files of url, response,
		response_size according to the input argument.
		set the time stamp as 0,and next pointer as NULL;
		return a pointer to the new block.
*/

Cache_t* construct_cache_block(char*  url, char* response,
	size_t response_size) {
    

    Cache_t* new_cache= Malloc(sizeof(Cache_t));   
    new_cache->url=Malloc(strlen(url)+1);

    strcpy(new_cache->url,url);

    new_cache->response = Malloc(response_size);

    memcpy(new_cache->response,response,response_size);
    new_cache->time_stamp=0;
    new_cache->response_size=response_size;
    new_cache->next=NULL;
    

    return new_cache;
 }

/*
	find_in_cache: given a url, find if it's in the cache.
	Return a pointer to the cache block when found it.
	Return NULL when not found.
*/
 
Cache_t* find_in_cache( char* url,Cache_t* cache) {
   	
   	Cache_t* p = cache;
 
    while(p) {
      
        if( strcmp(url,p->url) == 0 ) {
            
            return p;
        }
        p=p->next;
    }

    return NULL;
}

/*
	update_time_stamp: update the time stamp for all blocks in
	the cache, for the hitted cache, set it's stamp as 0, for others
	increase the time stamp by one. If there is a Miss(hit_cache==NULL)
	increase all blocks' time stamp.
*/

void update_time_stamp(Cache_t* hit_cache,Cache_t* cache) {
	Cache_t* p=cache;
	while(p) {
		if(p == hit_cache)
			p->time_stamp=0;
		else
			p->time_stamp=p->time_stamp+1;
		p=p->next;
	}
	return;
}

/*
	add_to_cache: add a new block to a cache
*/
int add_to_cache(Cache_t *new_block,Cache_t** cache) {
    new_block->next=*cache;
    *cache = new_block;
    return 1;
}


/*
	evict_cache: evict a block, with the largest time stamp
	(least-recently-used),and also update the total cache size;
	return -1 when find some error
	return 0 when success

*/

int evict_cache(size_t * total_cache_size,Cache_t** cache) {
    printf("evict cache\n");
    unsigned long time=0;
    Cache_t* p;
    Cache_t* cache_to_evic;

    p=*cache;
    if(!p) {
        printf("no cache to evinc\n");
        return -1;
    }
    //find the one to evict
   	cache_to_evic=NULL;
    while(p) {
        if(p->time_stamp >=time) {
            time=p->time_stamp;
            cache_to_evic=p;
        }
        p=p->next;
    }

    if(cache_to_evic==NULL) {
        printf("cache_to_evic==NULL\n");
        return -1;
    }


    //find the previous one block
    p=*cache;
    Cache_t* pre =NULL;
    while(p!=cache_to_evic) {
        pre=p;
        p=p->next;
    }

    if(pre==NULL) {
        *cache = p->next;
    }
    else{
        pre->next=p->next;
    }
    //update the cache size and free the evicted one
    *total_cache_size=*total_cache_size-(p->response_size);  
    free_cache_block(p);     
    return 0;
}

/*
	free_cache_block: free a given cache block
*/

void free_cache_block(Cache_t* cache_block) {
    if(!cache_block)
        return;
    Free(cache_block->url);
    Free(cache_block->response);
    Free(cache_block);
}


/*
	free_cache: free whole cache
*/
void free_cache(Cache_t* cache) {
    printf("freeing cache\n");
    Cache_t* p =cache;
    Cache_t* block_to_free=NULL;
    while(p){
        block_to_free=p;
        p=p->next;
        free_cache_block(block_to_free);
    }
    printf("free cache finished\n");
}



/*
	print_cache(for debugging):
	print all the blocks in cache
*/
void print_cache(size_t total_cache_size,Cache_t* cache) {
    Cache_t* p =cache;
    printf("print_cache_start********************************\n\n");
    printf("total_cache_size=%ld\n",(unsigned long)total_cache_size);
    printf("cache=%lx\n",(unsigned long)cache);
    int i=0;
    while(p) {
        printf("cache_block[%d]\n",i);
        printf("cache->time_stamp=%ld\n",p->time_stamp);
        printf("cache->response_size=%ld\n",p->response_size);
         i++;
        p=p->next;
    }
    printf("print_cache_end**********************************\n\n\n");
}