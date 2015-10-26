/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the power-of-two free list
 *             algorithm
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

#ifdef KMA_P2FL
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>

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
    kma_size_t size;
    kma_page_t* page;
    struct block_t* header;
    struct block_t* upLevel;
} blockT;

/************Global Variables*********************************************/
static blockT* flTable = NULL;
static int pageCount = 0;

/************Function Prototypes******************************************/
// returns block header of next order list of p2fl
blockT* makeNewLevel(blockT*, int, int, kma_page_t*);

// returns free block from list
blockT* getBlockFromList(kma_size_t, blockT*);

// returns first block of next page
blockT* getFirstAfterPage(blockT*, kma_page_t*);
/************External Declaration*****************************************/

/**************Implementation***********************************************/

void* kma_malloc(kma_size_t size){
    if (size > PAGESIZE)
        return NULL;
    else {

        if(flTable == NULL){
            // create first page and first block
            kma_page_t* page = get_page();
            int nextLevelAddr = (int)page->ptr;
            blockT* newLevel = (void*)nextLevelAddr;

            // make space for next buffer
            nextLevelAddr += sizeof(blockT);
            newLevel->header = (void*)nextLevelAddr;
            newLevel->page = page;
            newLevel->size = 0;

            // add all levels to table of free lists
            int p2fLevel = 16;
            flTable = newLevel;
            while(p2fLevel <= PAGESIZE)
            {
                nextLevelAddr += sizeof(blockT);
                newLevel = makeNewLevel(newLevel, p2fLevel, nextLevelAddr, page);
                p2fLevel <<=  1;
            }
            newLevel->upLevel = NULL;
        }

        // get rounded size necessary for incoming block
        kma_size_t rounded_size = 16;
        while(rounded_size < (size + sizeof(blockT)))
            rounded_size <<= 1;

        // find list header block corresponding to new rounded size
        blockT* curBlock = flTable->upLevel;
        while(curBlock->size < rounded_size)
            curBlock = curBlock->upLevel;

        // remove first available block from free list and point that block's header back to the list
        blockT* freeBlock = getBlockFromList(rounded_size, curBlock);
        curBlock->header = freeBlock->header;
        freeBlock->header = curBlock;
        return ((void*)freeBlock + sizeof(blockT));
    }
}

blockT* makeNewLevel(blockT* newLevel, int levelSize, int addr, kma_page_t* page){
    // set size pointer of input level to new address
    newLevel->upLevel = (void*)addr;
    // make new level at that address
    newLevel = newLevel->upLevel;
    newLevel->header = NULL;
    newLevel->size = levelSize;
    newLevel->page = page;
    return newLevel;
}

blockT* getBlockFromList(kma_size_t size, blockT* listHeader){
    // check what listHeader is pointing to
    blockT* freeBlock = listHeader->header;

    // if listHeader pointer is null, we need to add page of blocks to list
    if (freeBlock == NULL){

        // create new page for free list
        kma_page_t* page = get_page();
        pageCount++;
        int nextBlockAddr = (int)page->ptr;

        // create first block of free list (this is what is returned)
        blockT* curBlock = (void*)nextBlockAddr;
        blockT* first = curBlock;
        curBlock->upLevel = NULL;
        curBlock->size = size;
        curBlock->page = page;
        nextBlockAddr += size;

        // continue adding blocks to free list until we run out of space
        blockT* newBlock;
        while((nextBlockAddr - (int)page->ptr) < PAGESIZE)
        {
            newBlock = (void*)nextBlockAddr;

            // header should store pointer to next free buffer
            curBlock->header = newBlock;
            curBlock = newBlock;
            nextBlockAddr += size;
            newBlock->upLevel = NULL;
            newBlock->size = size;
            newBlock->page = page;
        }

        curBlock->header = NULL;
        listHeader->header = first;
        freeBlock = first;
    }

    return freeBlock;

}

void kma_free(void* ptr, kma_size_t size)
{

    // get block and corresponding list
    blockT* blockToFree = (blockT*)(ptr - sizeof(blockT));
    blockT* fromList = blockToFree->header;

    // insert block at front of free list
    blockToFree->header = fromList->header;
    fromList->header = blockToFree;

    // accumulate total amount of free space on block's page in list
    blockT* curBlock = blockToFree;
    kma_page_t* page = blockToFree->page;
    kma_size_t memOnPage = 0;
    while(curBlock != NULL)
    {
        if(curBlock->page == page)
            memOnPage += curBlock->size;
        curBlock = curBlock->header;
    }

    // if the page is full of free blocks, we should remove it
    if (memOnPage >= PAGESIZE){
        blockT* previous = fromList;
        blockT* current = previous->header;
        while(current){
            // iterate until finding blocks on page (may not be in one contiguous chain)
            if(current->page != page){
                previous = current;
                current = current->header;
            }
            else{
                // remove block from free list
                current = getFirstAfterPage(current, page);
                previous->header = current;

                // if still more blocks on list, keep going
                if (current){
                    previous = current;
                    current = current->header;
                }
            }
        }
        free_page(page);
        pageCount--;
    }

    // if no pages used in size table, free size table
    if(pageCount == 0){
        free_page(flTable->page);
        flTable = NULL;
    }
}

blockT* getFirstAfterPage(blockT* curBlock, kma_page_t* page){
    while (curBlock != NULL && curBlock->page == page)
        curBlock = curBlock->header;
    return curBlock;
}

#endif // KMA_P2FL

