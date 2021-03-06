/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the buddy algorithm
 *    Author: Stefan Birrer
 *    Copyright: 2004 Northwestern University
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    Revision 1.2  2009/10/31 21:28:52  jot836
 *    This is the current version of KMA project 3.
 *    It includes:
 *    - the most up-to-date handout (F'09)
 *    - updated skeleton including
 *        file-driven test harness,
 *        trace generator script,
 *        support for evaluating efficiency of algorithm (wasted memory),
 *        gnuplot support for plotting allocation and waste,
 *        set of traces for all students to use (including a makefile and README of the settings),
 *    - different version of the testsuite for use on the submission site, including:
 *        scoreboard Python scripts, which posts the top 5 scores on the course webpage
 *
 *    Revision 1.1  2005/10/24 16:07:09  sbirrer
 *    - skeleton
 *
 *    Revision 1.2  2004/11/05 15:45:56  sbirrer
 *    - added size as a parameter to kma_free
 *
 *    Revision 1.3  2004/12/03 23:04:03  sbirrer
 *    - initial version for the kernel memory allocator project
 *

 ************************************************************************/
 
 /************************************************************************
 Project Group: abg341, zta515
 
 ***************************************************************************/

#ifdef KMA_BUD
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

/************Private include**********************************************/
#include "kma_page.h"
#include "kma.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

#define MINBUFSIZE 32 
#define NUMBERBUF PAGESIZE / MINBUFSIZE

typedef struct {
  kma_page_t* next_page;
  uint8_t large;
  uint16_t length_longest[2 * NUMBERBUF - 1];
} page_header_t;

/************Global Variables*********************************************/
kma_page_t* first_page = NULL;

/************Function Prototypes******************************************/
void init_header(kma_page_t*);

kma_page_t* search_page(kma_size_t);

kma_page_t* next_free_page(void*);

kma_size_t real_size(int, kma_size_t);

void delete_page(kma_page_t*);

kma_size_t get_round(kma_size_t);
int get_child_left(int);
int get_child_right(int);
int get_parent(int);
int get_offset(int, kma_size_t);
bool is_pow2(int);
int max(int, int);

  
/************External Declaration*****************************************/

/**************Implementation**********************************************/

// initalize each pages header
void init_header(kma_page_t* page){
  page_header_t* page_header;
  page_header = (page_header_t*)(page->ptr);
  page_header->next_page = NULL;
  page_header->large = 0;

  int real_length;
  kma_size_t i, node_size = 2 * PAGESIZE, offset, header_offset;

  // don't allocate page header 
  header_offset = sizeof(page_header_t);

  for (i = 0; i < 2 * NUMBERBUF - 1; i++){
    if (is_pow2(i + 1)) node_size = node_size / 2;
    
    offset = get_offset(i, node_size);
    if (offset < header_offset){
      real_length = offset + node_size - header_offset;
      page_header->length_longest[i] = real_length > 0 ? real_length : 0;
    } else
      page_header->length_longest[i] = node_size;
  }
}

void* kma_malloc(kma_size_t size){
  if ((size + sizeof(kma_page_t*)) > PAGESIZE)
    return NULL;

  kma_size_t node_size;
  kma_size_t power_size;
  kma_size_t offset;

  kma_page_t* page;
  page_header_t* page_header;

  // search for page
  page = search_page(size);
  if (page == NULL)
    return NULL;
  else {
    page_header = page->ptr;
    if ((size + sizeof(page_header_t)) > PAGESIZE)
    {
      page_header->large = 1;
      return page->ptr + sizeof(kma_page_t*) + sizeof(uint8_t);
    }
  }

  if (!is_pow2(size))
    power_size = get_round(size);
  else
    power_size = size;

  if (power_size < MINBUFSIZE)
    power_size = MINBUFSIZE;

  int index = 0; 
  node_size = PAGESIZE;
  while (node_size != power_size){
    if (page_header->length_longest[get_child_left(index)] >= size)
      index = get_child_left(index);
    else
      index = get_child_right(index);
    node_size /= 2;
  }

  offset = get_offset(index, node_size) + node_size - page_header->length_longest[index];
  page_header->length_longest[index] = 0;

  while (index){
    index = get_parent(index);
    page_header->length_longest[index] = max(page_header->length_longest[get_child_left(index)], page_header->length_longest[get_child_right(index)]);
  }

  return page->ptr + offset;
}

void kma_free(void* ptr, kma_size_t size){
  kma_page_t* page = next_free_page(ptr);
  page_header_t* page_header = page->ptr;
  kma_size_t left_length, right_length;
  kma_size_t node_size;
  int index = 0;
  int offset;

  if (page_header->large == 1){
    delete_page(page);
    return;
  }
  
  node_size = MINBUFSIZE;
  offset = ptr - page->ptr;
  if ((offset % MINBUFSIZE) != 0)
    offset = offset - (offset % MINBUFSIZE);

  index = (offset + PAGESIZE) / node_size - 1;

  while (page_header->length_longest[index] != 0){
    index = get_parent(index);
    node_size = node_size * 2;
  }

  page_header->length_longest[index] = (uint16_t)real_size(index, node_size);

  while (index){
    index = get_parent(index);
    node_size = node_size * 2;
    left_length = page_header->length_longest[get_child_left(index)];
    right_length = page_header->length_longest[get_child_right(index)];

    // check length for coalescing
    if (left_length + right_length == real_size(index, node_size))
      page_header->length_longest[index] = (uint16_t)real_size(index, node_size);
    else
      page_header->length_longest[index] = max(left_length, right_length);
  }

  if (page_header->length_longest[0] == (PAGESIZE - sizeof(page_header_t)))
    delete_page(page);
}

kma_page_t* search_page(kma_size_t size)
{
  kma_page_t* page = first_page;
  page_header_t* page_header;

  // first page
  if (page == NULL){
    page = get_page();
    init_header(page);
    first_page = page;
    return page;
  }
  else {
    while (page != NULL){
      page_header = (page_header_t*)page->ptr;

      if (page_header->large != 1 && page_header->length_longest[0] >= size)
        return page;

      if (page_header->next_page == NULL){
        page_header->next_page = get_page();
        init_header(page_header->next_page);
        return page_header->next_page;
      }

      page = page_header->next_page;
    }
  }
  return NULL;
}

kma_page_t* next_free_page(void* ptr)
{
  kma_page_t* page = first_page;
  page_header_t* page_header;
  int offset;

  while (page){
    page_header = (page_header_t*)(page->ptr);
    offset = ptr - page->ptr;
    if (offset > 0 && offset < PAGESIZE)
      return page;
    page = page_header->next_page;
  }
  return NULL;
}

kma_size_t real_size(int index, kma_size_t node_size)
{
  kma_size_t size = node_size;
  int offset = get_offset(index, node_size);
  int occupied_offset = sizeof(page_header_t);

  if (occupied_offset >= offset){
    size = size - (occupied_offset - offset);
  }
  return size;
}

void delete_page(kma_page_t* page)
{
  kma_page_t* current_page = first_page;
  page_header_t* current_header = first_page->ptr;

  if (page->id == current_page->id){
    first_page = current_header->next_page;
    free_page(page);
  }
  else{
    while (current_header->next_page){
      if (current_header->next_page->id == page->id){
        current_header->next_page = ((page_header_t*)(page->ptr))->next_page;
        free_page(page);
        break;
      }
      current_page = current_header->next_page;
      current_header = (page_header_t*)(current_page->ptr);
    }
  }
}

int max(int x, int y){
  return x > y ? x : y;
}

kma_size_t get_round(kma_size_t size)
{
  size = size | (size >> 1);
  size = size | (size >> 2);
  size = size | (size >> 4);
  size = size | (size >> 8);
  size = size | (size >> 16);
  size += 1;
  return size;
}

bool is_pow2(int x){
  return(!(x & (x - 1)));
}

int get_offset(int x, kma_size_t size){
  return ((x+1)*size-PAGESIZE);
}

int get_parent(int x){
  return ((x+1)/2-1);
}

int get_child_left(int x){
  return (x*2+1);
}

int get_child_right(int x){
  return (x*2+2);
}
#endif // KMA_BUD