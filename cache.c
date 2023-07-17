#include "cache.h"
#include "dogfault.h"
#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// DO NOT MODIFY THIS FILE. INVOKE AFTER EACH ACCESS FROM runTrace
void print_result(result r) {
  if (r.status == CACHE_EVICT)
    printf(" [status: miss eviction, victim_block: 0x%llx, insert_block: 0x%llx]",
           r.victim_block_addr, r.insert_block_addr);
  if (r.status == CACHE_HIT)
    printf(" [status: hit]");
  if (r.status == CACHE_MISS)
    printf(" [status: miss, insert_block: 0x%llx]", r.insert_block_addr);
}

/* This is the entry point to operate the cache for a given address in the trace file.
 * First, is increments the global lru_clock in the corresponding cache set for the address.
 * Second, it checks if the address is already in the cache using the "probe_cache" function.
 * If yes, it is a cache hit:
 *     1) call the "hit_cacheline" function to update the counters inside the hit cache 
 *        line, including its lru_clock and access_counter.
 *     2) record a hit status in the return "result" struct and update hit_count 
 * Otherwise, it is a cache miss:
 *     1) call the "insert_cacheline" function, trying to find an empty cache line in the
 *        cache set and insert the address into the empty line. 
 *     2) if the "insert_cacheline" function returns true, record a miss status and the
          inserted block address in the return "result" struct and update miss_count
 *     3) otherwise, if the "insert_cacheline" function returns false:
 *          a) call the "victim_cacheline" function to figure which victim cache line to 
 *             replace based on the cache replacement policy (LRU and LFU).
 *          b) call the "replace_cacheline" function to replace the victim cache line with
 *             the new cache line to insert.
 *          c) record an eviction status, the victim block address, and the inserted block
 *             address in the return "result" struct. Update miss_count and eviction_count.
 */
result operateCache(const unsigned long long address, Cache *cache) {
  /* YOUR CODE HERE */
	
  // Increment lru clock of cache set
  unsigned long long set_index = cache_set(address, cache);
  cache->sets[set_index].lru_clock++;
  
  // Initialize result variable
  result r;
  r.insert_block_addr = 0;
  r.victim_block_addr = 0;
  r.status = 0;
  
  // printf("%s ",probe_cache(address,cache));
  // Probe cache for appropriate index
  if(probe_cache(address, cache)) {
      // printf("HIT");
      // If probe returns true, hit cacheline, increment hit count and update r status to hit
      hit_cacheline(address, cache);
      cache->hit_count++;
      r.status = CACHE_HIT;
 
  } else {
    // printf();
    // printf("MISS");
    // If probe returns false, find if there is an empty line to insert address
    if(insert_cacheline(address, cache) == true) {
      // printf("INSERTED");
      // Increment miss count and update r status to miss
      cache->miss_count++;
      r.status = CACHE_MISS;
      r.insert_block_addr = address_to_block(address,cache);

    } else {
      
      // Find a cache line based on LRU or LFU policy 
      unsigned long long victimBlockAddress = victim_cacheline(address,cache);
      // victimBlockAddress = address_to_block(victimBlockAddress,cache);
      // Replace cache line address
      replace_cacheline(victimBlockAddress,address,cache);

      // Update miss and eviction counter and update r status to evict
      cache->miss_count++;
      cache->eviction_count++;
      r.status = CACHE_EVICT;
      r.victim_block_addr = victimBlockAddress;
      r.insert_block_addr = address_to_block(address,cache);

    } 
  }
  return r;
}

// HELPER FUNCTIONS USEFUL FOR IMPLEMENTING THE CACHE
// Given an address, return the block (aligned) address,
// i.e., byte offset bits are cleared to 0
unsigned long long address_to_block(const unsigned long long address,
                                const Cache *cache) {
  // return address with block bits cleared
  return address & ~((1ULL << cache->blockBits) - 1);
}

// Return the cache tag of an address
unsigned long long cache_tag(const unsigned long long address,
                             const Cache *cache) {

  // Tag bits (ms part of memory address) # tag bits
  int tagBits = 64 - cache->blockBits - cache->setBits;

  // shift right by 64 - tag & return the # tag from least significant
  unsigned long long shiftedAddress = address >> (64-tagBits);
  
  // mask the tag bits
  unsigned long long tag = shiftedAddress & ((1ULL << tagBits) - 1);

  return tag;
}

// Return the cache set index of the address
unsigned long long cache_set(const unsigned long long address,
                             const Cache *cache) {
  
  // # of setbits
  int setBits = cache->setBits;

  // shift right by offset
  unsigned long long shiftedAddress = address >> cache->blockBits;

  // mask the sign bits
  unsigned long long set = shiftedAddress & ((1ULL << setBits) - 1);

  return set;
}

// Check if the address is found in the cache. If so, return true. else return false.
bool probe_cache(const unsigned long long address, const Cache *cache) {
  /* access cache set by set index, and check if tags are */
  unsigned long long setIndex = cache_set(address, cache);
  unsigned long long tag = cache_tag(address, cache);

  // find set, check each block in set, compare tag bits
  Set set = cache->sets[setIndex];

  // check each block in set
  for(int i = 0; i < cache->linesPerSet; i++){
    Line line = set.lines[i];

    if(line.valid && line.tag == tag){
      // printf("PROBE SUCCES");
      return true;
    }
  }
  
  return false;
}

// Access address in cache. Called only if probe is successful.
// Update the LRU (least recently used) or LFU (least frequently used) counters.
void hit_cacheline(const unsigned long long address, Cache *cache){
  /* YOUR CODE HERE */
  int i = 0;

  // Get set address with cache set
  unsigned long long set_address = cache_set(address, cache);
  
  // Get tag/block address with cache tag
  unsigned long long cached_tag = cache_tag(address, cache);

  // Iterate through cache set to find valid tag
  for(i = 0; i < cache->linesPerSet; i++) {
    
    // If tag line is valid, if tag line matches
    if(cache->sets[set_address].lines[i].valid
      && (cache->sets[set_address].lines[i].tag == cached_tag)) {

      // Update LFU/access counter & LRU/lru clock
      if(cache->lfu == 0){
        // Lru
        cache->sets[set_address].lines[i].lru_clock = cache->sets[set_address].lru_clock;
      } else {
        cache->sets[set_address].lines[i].access_counter++;
      }

      // insert complete
      return;
    }
  } 
}

/* This function is only called if probe_cache returns false, i.e., the address is
 * not in the cache. In this function, it will try to find an empty (i.e., invalid)
 * cache line for the address to insert. 
 * If it found an empty one:
 *     1) it inserts the address into that cache line (marking it valid).
 *     2) it updates the cache line's lru_clock based on the global lru_clock 
 *        in the cache set and initiates the cache line's access_counter.
 *     3) it returns true.
 * Otherwise, it returns false.  
 */ 
bool insert_cacheline(const unsigned long long address, Cache *cache) {
  // Find empty cache block in set
  unsigned long long setIndex = cache_set(address, cache);
  Set *set = &(cache->sets[setIndex]);

  for(int i = 0; i < cache->linesPerSet; i++){
    Line *l = &(set->lines[i]);

    // empty block
    if(l->valid == false){
      // Initlize new cache block
      l->valid = true;
      l->block_addr = address_to_block(address,cache);
      l->tag = cache_tag(address,cache);
      l->lru_clock = set->lru_clock;
      l->access_counter = 1;
      return true;
    }
  }
  // no space
  return false;
}

// If there is no empty cacheline, this method figures out which cacheline to replace
// depending on the cache replacement policy (LRU and LFU). It returns the block address
// of the victim cacheline; note we no longer have access to the full address of the victim
unsigned long long victim_cacheline(const unsigned long long address,
                                const Cache *cache) {
  /* YOUR CODE HERE */
  unsigned long long vict_add = 0;

  unsigned long long setIndex = cache_set(address,cache);

  // assign MAximum Unsigned long long value
  unsigned long long minLru = INT64_MAX;
  int minaccess = INT32_MAX;

  for(int i = 0; i < cache->linesPerSet; i++){
    Line l = cache->sets[setIndex].lines[i];

    // 0: Least Recently Used (LRU), 1: Least Frequently Used (LFU)
    if ((cache->lfu == 0 && l.lru_clock < minLru) ||
        (cache->lfu == 1 && l.access_counter < minaccess)) {
      vict_add = l.block_addr; // Update the victim block address
      minLru = l.lru_clock; // Update the minimum lru_clock
      minaccess = l.access_counter; // Update the minimum access_counter
    } else if(cache->lfu == 1 && l.access_counter == minaccess){
      // use LRU to decide
      if(l.lru_clock < minLru){
        vict_add = l.block_addr;
        minLru = l.lru_clock;
      }
    }
  }

  // return address based on LRU or LFU
   return vict_add;
}

/* Replace the victim cacheline with the new address to insert. Note for the victim cachline,
 * we only have its block address. For the new address to be inserted, we have its full address.
 * Remember to update the new cache line's lru_clock based on the global lru_clock in the cache
 * set and initiate the cache line's access_counter.
 */
void replace_cacheline(const unsigned long long victim_block_addr,
		       const unsigned long long insert_addr, Cache *cache) {
  /* YOUR CODE HERE */

  // Find insert_block with cache_tag
  unsigned long long ins_tag = cache_tag(insert_addr, cache);

  // Find insert_set with cache_set
  unsigned long long ins_set = cache_set(insert_addr, cache);
  
  int i = 0;

  Set *set = &(cache->sets[ins_set]);
  // Victim and insert_block share the same set, so iterate through insert_block set and find victim block
  for(i = 0; i < cache->linesPerSet; i++) {
    Line *line = &(set->lines[i]);
    
    if(line->block_addr == victim_block_addr) {
      line->valid = true;
      line->tag = ins_tag;
      line->block_addr = address_to_block(insert_addr,cache);
      line->lru_clock = set->lru_clock;
      line->access_counter = 1; 
    }

  }
}

// allocate the memory space for the cache with the given cache parameters
// and initialize the cache sets and lines.
// Initialize the cache name to the given name 
void cacheSetUp(Cache *cache, char *name) {
  /* YOUR CODE HERE */
  int i = 0;
  int j = 0;

  

  // Allocating memory space for cache parameters
  cache->sets = malloc(sizeof(Set) * pow(2, (cache->setBits)));

  // Use a for loop
  

  //Initialize cache sets and lines
  for(i = 0; i < pow(2, (cache->setBits)); i++) {
    
    unsigned long long set_address = i;

    // Allocate memory for lines
    cache->sets[set_address].lines = malloc(sizeof(Line) * pow(2, (cache->linesPerSet)) * pow(2, (cache->blockBits)));
    
    // Set global set lru_clock to 0
    cache->sets[i].lru_clock = 0;
    
    for(j = 0; j < cache->linesPerSet; j++) {

      // Set valid boolean value
      cache->sets[set_address].lines[j].valid = false;
      cache->sets[set_address].lines[j].lru_clock = 0;
      cache->sets[set_address].lines[j].access_counter = 0;
    
    }
  
  }

  // Initialize cache name to given name
  cache->name = name;
  }

// deallocate the memory space for the cache
void deallocate(Cache *cache) {
  /* YOUR CODE HERE */
  // Deallocate memory for each cache set
  for (int i = 0; i < (1 << cache->setBits); i++) {
    Set *set = &(cache->sets[i]);
    free(set->lines); // Deallocate memory for the lines in the set
  }

  // Deallocate memory for the cache sets
  free(cache->sets);

  // Deallocate memory for the cache structure itself
  // free(cache);
}

// print out summary stats for the cache
void printSummary(const Cache *cache) {
  printf("%s hits: %d, misses: %d, evictions: %d\n", cache->name, cache->hit_count,
         cache->miss_count, cache->eviction_count);
}