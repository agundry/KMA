/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the resource map algorithm
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
 *    Revision 1.1  2004/11/03 23:04:03  sbirrer
 *    - initial version for the kernel memory allocator project
 *
 *    Revision 1.3  2004/12/03 23:04:03  sbirrer
 *    - initial version for the kernel memory allocator project
 *
 
 ************************************************************************/

/************************************************************************
 Project Group: abg341, zta515
 
 ***************************************************************************/

#ifdef KMA_RM
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

/************Private include**********************************************/
#include "kma_page.h"
#include "kma.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

typedef struct block_t
{
  bool isFree;
  struct block_t* next;
  struct block_t* prev;
} blockT;

/************Global Variables*********************************************/
kma_page_t* firstPage = NULL;
/************Function Prototypes******************************************/

// gather amount of free memory in block
int getBlockSize(blockT*);

// update the status of a block
void updateBlock(blockT*, blockT*, blockT*, bool);

// find next free block
blockT* getNextFree(kma_page_t*, kma_size_t);

// get free space after block
int getFreeSpace(blockT*, blockT*);

// check if neighbor in given direction is free
bool isNeighborFree(blockT*, int);

// coalesce with neighbors
void coalesce(blockT*);

/************External Declaration*****************************************/

/**************Implementation***********************************************/


void*
kma_malloc(kma_size_t size)
{
    if(size > PAGESIZE)
        return NULL;
    else {
        if(firstPage == NULL){
            // Initialize first page with new block
            firstPage = get_page();
            *((kma_page_t**)firstPage->ptr) = firstPage;
            blockT* firstBlock = (blockT*)(firstPage->ptr + sizeof(kma_page_t*));

            // set attributes of new block
            updateBlock(firstBlock, NULL, NULL, TRUE);
        }


        // find next free block, make it unavailable
        blockT* curBlock = getNextFree(firstPage, size);
        curBlock->isFree = FALSE;

        // set pointer to newBlock
        blockT* newBlock = (blockT*)((void*)curBlock + sizeof(*curBlock) + size);

        // calculate free remaining space after block (on same page)
        int freeSpace = getFreeSpace(curBlock, newBlock);

        // if big enough to fit a standard block, insert into linked list
        if(freeSpace > sizeof(blockT))
        {
            updateBlock(newBlock, curBlock, curBlock->next, TRUE);
            curBlock->next = newBlock;
            if(newBlock->next != NULL)
                newBlock->next->prev = newBlock;
        }


        // return pointer to allocated memory
        return (void*)curBlock + sizeof(*curBlock);
    }
}


void
kma_free(void* ptr, kma_size_t size)
{
    if(ptr == NULL)
        return;
    else{
        blockT* curBlock = (blockT*)(ptr - sizeof(blockT));

        // Handle coalescing
        coalesce(curBlock);

        // Find first block of page
        blockT* firstBlock = (blockT*)(BASEADDR(curBlock) + sizeof(kma_page_t*));

        // If page only contains one block
        if(getBlockSize(firstBlock) >= PAGESIZE - sizeof(*firstBlock) - sizeof(kma_page_t*)){
            if(firstBlock == (blockT*)(firstPage->ptr + sizeof(kma_page_t*))){
                // if first page is also last page
                if(firstBlock->next == NULL)
                    firstPage = NULL;
                else
                    firstPage = *((kma_page_t**)BASEADDR(firstBlock->next));
            }

            if(firstBlock->prev != NULL)
                firstBlock->prev->next = firstBlock->next;

            if(firstBlock->next != NULL)
                firstBlock->next->prev = firstBlock->prev;

            free_page(*((kma_page_t**)BASEADDR(curBlock)));
        }
    }
}

int getBlockSize(blockT* block){
    if (block == NULL)
        return -1;
    else if (block -> next != NULL && BASEADDR(block) ==  BASEADDR(block->next))
        return (void*)(block->next) - (void*)block - sizeof(*block);
    else {
        void* lastPage = BASEADDR(block) + PAGESIZE;
        return lastPage - (void*)block - sizeof(*block);
    }
}

void updateBlock(blockT* block, blockT* newPrev, blockT* newNext, bool newBool){
    block->prev = newPrev;
    block->next = newNext;
    block->isFree = newBool;
}

blockT* getNextFree(kma_page_t* firstPage, kma_size_t size){
        // get pointer to next block start
        blockT* nextBlock = (blockT*)(firstPage->ptr + sizeof(kma_page_t*));

        // iterate until we find a block that is fre and can contain this size
        while(!(nextBlock->isFree && size < getBlockSize(nextBlock))){
            //if current block is at end of page
            if(nextBlock->next == NULL)
            {
                // allocate new page and new first block
                kma_page_t* nextPage = get_page();
                *((kma_page_t**)nextPage->ptr) = nextPage;
                blockT* nextFirstBlock = (blockT*)(nextPage->ptr + sizeof(kma_page_t*));
                nextBlock->next = nextFirstBlock;

                // set attributes of iterating block
                updateBlock(nextFirstBlock, nextBlock, NULL, TRUE);
            }
            nextBlock = nextBlock->next;
        }
        return nextBlock;
}

int getFreeSpace(blockT* curBlock, blockT* newBlock){
    if(curBlock->next != NULL && BASEADDR(curBlock)==BASEADDR(curBlock->next))
        // free space is space between next block and end of new block
        return (void*)(curBlock->next) - (void*)newBlock;
    else
        // free space is space between start of next page and end of new block
        return BASEADDR(curBlock) + PAGESIZE - (void*)newBlock;
}

bool isNeighborFree(blockT* curBlock, int direction){
    blockT* neighbor;
    // get neighbor
    if (direction == 1)
        neighbor = curBlock->next;
    else
        neighbor = curBlock->prev;

    // determine if neighbor is not null, is on same page, and is free
    if (neighbor == NULL)
        return FALSE;
    if (BASEADDR(curBlock)!=BASEADDR(neighbor))
        return FALSE;
    if (!neighbor->isFree)
        return FALSE;
    return TRUE;
}

void coalesce(blockT* curBlock){

    // if both neighbors are free
    if (isNeighborFree(curBlock, 0) && isNeighborFree(curBlock, 1)) {
        // combine all three blocks
        curBlock->prev->next = curBlock->next->next;
        if (curBlock->next->next != NULL)
            curBlock->next->next->prev = curBlock->prev;
    }
    // if previous neighbor is free
    else if (isNeighborFree(curBlock, 0)) {
        // combine with previous neighbor
        curBlock->prev->next = curBlock->next;
        if(curBlock->next != NULL)
            curBlock->next->prev = curBlock->prev;
    }
    // if forward neighbor is free
    else if (isNeighborFree(curBlock, 1)) {
        // combine with forward neighbor and set to available
        curBlock->isFree = TRUE;
        curBlock->next = curBlock->next->next;
        if(curBlock->next != NULL)
            curBlock->next->prev = curBlock;
    }
    // if no neighbor is free, no coalescing necessary
    else
        curBlock->isFree = TRUE;

}

#endif // KMA_RM
