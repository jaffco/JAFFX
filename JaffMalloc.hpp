#include <cstring>
#include <stdio.h> // for printf

namespace Jaffx {
  //Taken from https://electro-smith.github.io/libDaisy/md_doc_2md_2__a6___getting-_started-_external-_s_d_r_a_m.html
  #ifndef DAISY_SDRAM_BASE_ADDR
  #define DAISY_SDRAM_BASE_ADDR 0xC0000000
  #define DAISY_SDRAM_SIZE 67108864 //64 * 1024 * 1024 = 64 MB
  #endif
  #ifndef byte
  #define byte unsigned char
  #endif

  // singleton class for managing SDRAM throughout a program's lifecycle
  class MyMalloc {
  private:
    byte* pBackingMemory = (byte*)DAISY_SDRAM_BASE_ADDR;

    //Bookkeeping struct
    typedef struct metadata_stc {
      struct MyMalloc::metadata_stc* next;
      struct MyMalloc::metadata_stc* prev;
      unsigned int size;
      bool allocatedOrNot;
      byte* buffer;
    } metadata;

    MyMalloc::metadata* freeSectionsListHeadPointer;

  public:
    MyMalloc() {}
    //constructor
    void MyMallocInit() {
      //Actually initialize the 24-byte struct at the beginning - careful as this might segfault later when `initialStruct` goes out of scope
      MyMalloc::metadata initialStruct;
      initialStruct.next = nullptr;
      initialStruct.prev = nullptr;
      initialStruct.size = DAISY_SDRAM_SIZE - sizeof(MyMalloc::metadata);
      initialStruct.allocatedOrNot = false;
      initialStruct.buffer = (byte*)(&(this->pBackingMemory[0]) + sizeof(MyMalloc::metadata));

      // Putting initial struct into start of BigBuffer so it is accessible outside this scope and we can avoid segfaults
      storeMetadataStructInBigBuffer(this->pBackingMemory, initialStruct);

      this->freeSectionsListHeadPointer = (metadata*)&(this->pBackingMemory[0]);
    }

    //Have copy & copy-assignment constructors disabled to enforce singleton as only instance
    MyMalloc(const MyMalloc&) = delete;
    void operator=(const MyMalloc&) = delete;
    ~MyMalloc() {} // Don't need a destructor because memory will zero-fill on init

  private:
    /*******************************Helper functions*************************/

    /**
     * @brief This function returns whether the requested memory pointer is in range of the SDRAM
     *
     * @param pBufferPos
     * @return `true` - Pointer is valid and operations can continue
     * @return `false` - Pointer is already invalid, break and maybe alert?
     */
    bool pointerInMemoryRange(byte* pBufferPos) {
      return ( (pBufferPos >= this->pBackingMemory) && 
      (&(this->pBackingMemory[DAISY_SDRAM_SIZE]) > pBufferPos) );
    }

    /**
     * @brief Given a `metadata` struct, split it byte-wise and write it into SDRAM
     * Does not modify the original structure, nor connect the original struct to the buffer
     *
     * @param pBufferPos The array index of SDRAM at which to insert the data
     * @param inputMetadata The struct to copy into the buffer
     */
    void storeMetadataStructInBigBuffer(byte* pBufferPos, metadata inputMetadata) {
      //Check pointer bounds to ensure that the requested read pointer is within the BigBuffer
      if (!pointerInMemoryRange(pBufferPos)) {
        //TODO: Alert or maybe serial print somehow
        return;
      }
      MyMalloc::metadata* pMetadataStruct = (MyMalloc::metadata*)pBufferPos;
      *pMetadataStruct = inputMetadata; 
    }

    /**
     * @brief Get the `metadata` struct from `BigBuffer` at a given position
     *
     * @param pBufferPos The position from which to retrieve the `metadata` struct
     * @return The `metadata` struct found at the given position in `BigBuffer`
     */
    MyMalloc::metadata getMetadataStructInBigBuffer(byte* pBufferPos) {
      //Check pointer bounds to ensure that the requested read pointer is within the BigBuffer
      if (!pointerInMemoryRange(pBufferPos)) {
        //TODO: Alert or maybe serial print somehow
        return *((MyMalloc::metadata*) nullptr); //TODO: This violently fails, maybe return an empty or destroyed one
      }
      MyMalloc::metadata pMetadataStruct = *((MyMalloc::metadata*)pBufferPos);
      return pMetadataStruct;
    }
    /************************************************************************/

  public:
    /**
     * @brief acts just as stdlib::malloc with a couple of differences
     *
     * - Returns nullptr if there is not enough space to malloc the requested size
     *
     * - Returns nullptr if requested size = 0 (with no space allocated for it)
     *
     * @param requestedSize The number in bytes of how much data you want allocated in SDRAM
     * @return void* - Pointer to a contiguous array in SDRAM, or `nullptr` if errors
     */
    unsigned int round8Align(unsigned int a) {
      return (a % 8) ? 8 - (a % 8) + a : a; //Rounds up to nearest multiple of 8
    }

    void* malloc(size_t requestedSize) {
      if (requestedSize <= 0) return nullptr; //Safety check
      //If their requested size is not already divisible by 8, make it so
      unsigned int actualSize = this->round8Align(requestedSize);
      if (actualSize == 0) return nullptr;

      //Loop through all the values in the list of "free" sections and find the first one that has enough room
      MyMalloc::metadata* freeStruct = this->freeSectionsListHeadPointer;
      /*
      TODO: For now, the hijacking policy is to first fill up a larger empty space and THEN, if we don't find anything that
      will fit, iterate through once again to try and hijack an entire space

      so freeing up 64 but then trying to malloc 48 would be 48+20=68 bytes that don't technically fit, and you'd search elsewhere
      before coming back to the 64 and stealing it

      Later on, we should know to hijack the 64 and look no further because we leave a much larger space
      */
      while (freeStruct != nullptr) {
        if (freeStruct->size >= (actualSize + sizeof(MyMalloc::metadata))) {
          // Initializing new struct for new free space
          MyMalloc::metadata newFreeStruct;
          newFreeStruct.next = freeStruct->next;
          newFreeStruct.prev = freeStruct->prev;
          newFreeStruct.size = freeStruct->size - actualSize - sizeof(MyMalloc::metadata);
          newFreeStruct.allocatedOrNot = false;
          newFreeStruct.buffer = freeStruct->buffer + actualSize + sizeof(MyMalloc::metadata);

          // Initializing pointer to space in DRAM (old buffer + requested space)
          MyMalloc::metadata* pBufferNewFreeStruct = (MyMalloc::metadata*)(freeStruct->buffer + actualSize);
          this->storeMetadataStructInBigBuffer((byte*)pBufferNewFreeStruct, newFreeStruct);

          // Adjusting pointers to point to new free space
          if (freeStruct->next) {
            (freeStruct->next)->prev = pBufferNewFreeStruct;
          }
          if (freeStruct->prev) {
            (freeStruct->prev)->next = pBufferNewFreeStruct;
          }
          if (freeStruct == this->freeSectionsListHeadPointer) {
            this->freeSectionsListHeadPointer = pBufferNewFreeStruct;
          }

          // Adjusting previously-free struct to specified input values
          freeStruct->size = actualSize;
          freeStruct->next = nullptr;
          freeStruct->prev = nullptr;
          freeStruct->allocatedOrNot = true;
          if (freeSectionsListHeadPointer->allocatedOrNot) {
            freeSectionsListHeadPointer = nullptr;
          }
          return freeStruct->buffer;
        }
        freeStruct = freeStruct->next; //Go to next
      }
      //If we were not able to find a free struct whose buffer could fill a metadata + dataSize, then we look into hijacking a free struct of similar size
      freeStruct = freeSectionsListHeadPointer;
      while (freeStruct != nullptr) {
        if (freeStruct->size >= (actualSize)) {

          // remove this struct from free list because it has been "hijacked"
          if (freeStruct->next) {
            (freeStruct->next)->prev = freeStruct->prev;
          }
          if (freeStruct->prev) {
            (freeStruct->prev)->next = freeStruct->next;
          }


          // set next and prev pointers to NULL because block is no longer part of free list
          freeStruct->next = nullptr;
          freeStruct->prev = nullptr;
          freeStruct->allocatedOrNot = true;
          if (freeSectionsListHeadPointer->allocatedOrNot) {
            freeSectionsListHeadPointer = nullptr;
          }
          return freeStruct->buffer;
        }
        freeStruct = freeStruct->next;
      }
      //If we were unable to find any free struct of any size, we need to return a nullptr to indicate that allocation was not possible
      return nullptr;
    }

    /**
     * @brief acts just as stdlib::calloc with a couple of differences
     *
     * - Returns nullptr if there is not enough space to malloc the requested size
     *
     * - Returns nullptr if requested size = 0 (with no space allocated for it)
     *
     * - For now, does not make initialization of memory any more efficient as it relies
     *   on `memset` to zero-fill, maybe a future TODO to have the zero-ing done on the
     *   hardware side
     *
     * @param numElements Number of elements in the array
     * @param size Size of each element
     * @return void* - Pointer to a contiguous array in SDRAM, or `nullptr` if errors
     */
    void* calloc(size_t numElements, size_t size) {
      size_t arrSizeInBytes = numElements * size;
      void* returnVal = MyMalloc::malloc(arrSizeInBytes);
      if (returnVal) {
        //This means the `malloc` successfully worked, we just need to zero-fill the entire buffer now
        ::memset(returnVal, 0, arrSizeInBytes);
      }
      return returnVal; //Either way we either return nullptr or a successful buffer
    }

    /**
     * @brief acts just as stdlib::realloc with a couple of differences
     *
     * - Frees array and returns nullptr if requested size = 0
     *
     * - `malloc()`s a new array of `size` if `ptr` is `nullptr`
     *
     * @param ptr
     * @param size
     * @return void*
     */
    void* realloc(void* ptr, size_t size) {
      if (size == 0) {
        this->free(ptr);
        return nullptr;
      }

      if (!ptr) {
        return this->malloc(size);
      }

      // Retrieve metadata of the current block
      MyMalloc::metadata* pCurrentMetadata = (MyMalloc::metadata*)((byte*)ptr - sizeof(MyMalloc::metadata));
      MyMalloc::metadata currentMetadata = this->getMetadataStructInBigBuffer((byte*)pCurrentMetadata);

      if (size <= currentMetadata.size) { // If new size is smaller or equal to current size, truncate the block
          unsigned int adjustedNewSize = this->round8Align(size);
          if (adjustedNewSize < currentMetadata.size) {
            //remainingSize is how much overall space is leftover within our allocated block (including any space for metadata)
            unsigned int remainingSize = currentMetadata.size - adjustedNewSize;

            if (remainingSize >= sizeof(MyMalloc::metadata)) { //This means we can fit a new free block in the remaining space
              // Create a new free block with the remaining space - fudge it as full so that free goes ahead and frees it
              MyMalloc::metadata* pNewFreeBlock = (MyMalloc::metadata*)(pCurrentMetadata->buffer + adjustedNewSize);
              
              MyMalloc::metadata newFreeBlock;
              newFreeBlock.size = remainingSize - sizeof(MyMalloc::metadata);
              //These pointers will automatically be filled in by free()
              newFreeBlock.next = nullptr;
              newFreeBlock.prev = nullptr;
              newFreeBlock.allocatedOrNot = true; //Set true so that free() runs on it
              newFreeBlock.buffer = (byte*)pNewFreeBlock + sizeof(MyMalloc::metadata);
              
              this->storeMetadataStructInBigBuffer((byte*)pNewFreeBlock, newFreeBlock);

              // Add the new free block to the free list
              this->free(pNewFreeBlock->buffer);
            } //If not, we'll just keep it as is with the extra room

            // Update the current block's metadata
            currentMetadata.size = adjustedNewSize;
            this->storeMetadataStructInBigBuffer((byte*)pCurrentMetadata, currentMetadata);
          } //else that means that we are perfect size and/or rounding errors in which case we shouldn't truncate and just return the OG ptr anyways
          return ptr; //Just return the original value because we didn't move anything, we just trimmed
      }
      else { //They are requesting more space (probably the more frequent use-case)
        unsigned int additionalRequiredSize = this->round8Align(size - currentMetadata.size);
        //First we search for the next forward-adjacent free block
        //We are guaranteed by correctness that the next address after this allocated section represents a valid metadata obj if it falls within the bounds of our SDRAM
        MyMalloc::metadata* pNextForwardAdjacentMetadata = (MyMalloc::metadata*)((byte*)ptr + currentMetadata.size);
        if (this->pointerInMemoryRange((byte*)pNextForwardAdjacentMetadata) && !(pNextForwardAdjacentMetadata->allocatedOrNot)) {
          //Then the forward-adjacent block is free and we can either split it or hijack it entirely
          unsigned int totalAvailableSizeInAdjFreeBlock = pNextForwardAdjacentMetadata->size + sizeof(MyMalloc::metadata);
            if (totalAvailableSizeInAdjFreeBlock >= additionalRequiredSize) {
              //This means we will be able to stretch our current allocation
              if (totalAvailableSizeInAdjFreeBlock >= additionalRequiredSize + sizeof(MyMalloc::metadata)) {
                //This means we can fit the entire 8-aligned extra space and still fit a free block (we'll call coalesce)
                // Split the forward-adjacent block
                MyMalloc::metadata newFreeBlock;
                newFreeBlock.size = totalAvailableSizeInAdjFreeBlock - additionalRequiredSize - sizeof(MyMalloc::metadata); // Remaining space after split
                newFreeBlock.next = pNextForwardAdjacentMetadata->next;
                newFreeBlock.prev = pNextForwardAdjacentMetadata->prev;
                newFreeBlock.allocatedOrNot = false;
                newFreeBlock.buffer = pNextForwardAdjacentMetadata->buffer + additionalRequiredSize;

                // Store the new free block metadata
                MyMalloc::metadata* pNewFreeBlock = (MyMalloc::metadata*)((byte*)newFreeBlock.buffer - sizeof(MyMalloc::metadata));
                this->storeMetadataStructInBigBuffer((byte*)pNewFreeBlock, newFreeBlock);

                //Now update the neighboring free blocks to point to this one instead of the old one
                if (pNewFreeBlock->prev) {
                  pNewFreeBlock->prev->next = pNewFreeBlock;
                }
                if (pNewFreeBlock->next) {
                  pNewFreeBlock->next->prev = pNewFreeBlock;
                }
                if (pNextForwardAdjacentMetadata == this->freeSectionsListHeadPointer) {
                  this->freeSectionsListHeadPointer = pNewFreeBlock;
                }

                // Update the current block metadata
                currentMetadata.size += additionalRequiredSize;
                //currentMetadata.next = pNewFreeBlock;
                this->storeMetadataStructInBigBuffer((byte*)pCurrentMetadata, currentMetadata);
              }
              else {
                //We cannot fit the extra space AND a free block so we must hijack the entire space
                currentMetadata.size += totalAvailableSizeInAdjFreeBlock; // Expand the current block's size
                currentMetadata.next = pNextForwardAdjacentMetadata->next; // Skip over the next block we are about to replace

                // Update the linked list to remove the forward-adjacent block
                if (pNextForwardAdjacentMetadata->next) {
                  pNextForwardAdjacentMetadata->next->prev = pCurrentMetadata;
                }
                // Update the struct with the newest values in SDRAM
                this->storeMetadataStructInBigBuffer((byte*)pCurrentMetadata, currentMetadata);    
              }
              return ptr; //Return the same pointer since we were able to increase the space without moving data
            }
          }

        //Then that means this is at the end of the line, we cannot increase the size and must attempt to re-malloc to a new place
        //TODO: Maybe add backwards check for better memory efficiency and lesser fragmentation

        //If we cannot find enough room for the expansion in a forward-adjacent free block, 
        // we need to search elsewhere
        void* newBuffer = this->malloc(size);
        if (!newBuffer) {
          return nullptr; // Allocation failed
        }

        // Copy existing data to the new block
        ::memcpy(newBuffer, ptr, currentMetadata.size);

        // Free the old block
        this->free(ptr);
        return newBuffer;
      }
    }
  private:
    /**
     * @brief Private helper function dedicated to coalescing all the free spaces one at a time; meant to be
     * used in multiple runs until the function finally returns false
     *
     * @return true - this means that some spaces were coalesced
     * @return false - this means that no free spaces were adjacent to coalesce and unless something is freed, we cannot further coalesce
     */
    bool coalesceFreeSpaces() {
      bool returnVal = false;
      MyMalloc::metadata* pCurrentStruct = this->freeSectionsListHeadPointer;
      if (pCurrentStruct == nullptr) {
        return returnVal;
      }
      MyMalloc::metadata* pNextStruct = pCurrentStruct->next;
      while (pNextStruct != nullptr) {
        // Coalescing adjacent free spaces
        if ((MyMalloc::metadata*)(pCurrentStruct->buffer + pCurrentStruct->size) == pNextStruct) {
          //Then the next one borders this one and we can coalesce the two
          pCurrentStruct->size = pCurrentStruct->size + sizeof(MyMalloc::metadata) + pNextStruct->size; //Reset the size
          //pCurrentStruct->prev = pCurrentStruct->prev | pNextStruct->prev;
          //Now remove the next struct
          if (pNextStruct->next) {
            (pNextStruct->next)->prev = pCurrentStruct; //make the next next one point to this one
          }
          pCurrentStruct->next = pNextStruct->next; //point the next next one to the next one
          //This means that we coalesced at least 2 spaces together, raise the return bit flag
          returnVal |= true;
        }
        //Actual iteration
        pNextStruct = pNextStruct->next;
        pCurrentStruct = pCurrentStruct->next;
      }
      return returnVal;
    }

  public:
    /**
     * @brief acts just as stdlib::free
     *
     * - Works on memory allocated by the use of accompanying `malloc`/`calloc`/`realloc` calls
     *
     * - Will not do anything if requested block is already freed
     *
     * - Undefined behavior if you pass in a pointer to something that was NOT allocated using the accompanying
     *   `malloc`/`calloc`/`realloc` calls from here
     * -
     *
     * @param pBuffer Pointer to the data you want freed, previously allocated by `malloc`/`calloc`/`realloc`
     */
    void free(void* pBuffer) {
      if (!pBuffer) { // In case they try passing in zero or nullptr
        return;
      }
      if (!(this->pointerInMemoryRange((byte*)pBuffer))) {
        return; //The pointer they passed isn't within SDRAM addressable space, which means it was not `malloc`ated by any of our calls
      }
      MyMalloc::metadata* pMetadataToFree = (MyMalloc::metadata*)((byte*)pBuffer - sizeof(MyMalloc::metadata));
      MyMalloc::metadata metadataToFree = this->getMetadataStructInBigBuffer((byte*)pMetadataToFree);
      //If it is already freed, don't do anything else
      if (!(metadataToFree.allocatedOrNot)) {
        return;
      }

      /*
      - Loop through the free space list, for each item we want to see if the free sections are adjacent
      - If they are adjacent, combine them into one object and continue
      */
      metadata* pCurrentStruct = this->freeSectionsListHeadPointer;
      //If the head of free list is NULL, then by freeing this element we make it the beginning of the list
      if (pCurrentStruct == nullptr) {
        this->freeSectionsListHeadPointer = pMetadataToFree;
        pCurrentStruct = this->freeSectionsListHeadPointer;
      }

      //This boolean keeps track of whether or not we have already actually freed the object
      bool alreadyInsertedOrNot = false;
      // If requested buffer is before current FreeList head, replace head
      if (metadataToFree.buffer < pCurrentStruct->buffer) {
        MyMalloc::metadata* pOldNext = this->freeSectionsListHeadPointer;
        this->freeSectionsListHeadPointer = pMetadataToFree;
        pOldNext->prev = this->freeSectionsListHeadPointer;
        this->freeSectionsListHeadPointer->next = pOldNext;
        pMetadataToFree->allocatedOrNot = false;
        pCurrentStruct = this->freeSectionsListHeadPointer; //Restart to beginning of list
        alreadyInsertedOrNot = true; //Since this also counts as an insert
      }

      MyMalloc::metadata* pNextStruct = pCurrentStruct->next;
      while (pCurrentStruct != nullptr) {
        //First we want to try to insert this new free space
        if (!alreadyInsertedOrNot) { //Don't keep re-inserting into the list or this will loop indefinitely as we add more nodes on the fly
          if (pNextStruct) {
            if ((pCurrentStruct->buffer <= metadataToFree.buffer && pNextStruct->buffer >= metadataToFree.buffer)) {
              //The metadataToFree->buffer is in between the current struct and the next struct, so we insert after current struct
              MyMalloc::metadata* pOldNext = pCurrentStruct->next;
              if (pOldNext != nullptr) {
                pOldNext->prev = pMetadataToFree;
              }
              pCurrentStruct->next = pMetadataToFree;

              //connect the current struct to the doubly linked list
              if (pMetadataToFree != pCurrentStruct) {
                //In the off chance that this IS the new head, we don't want to set the previous to itself
                pMetadataToFree->prev = pCurrentStruct;
              }
              pMetadataToFree->next = pNextStruct;

              pMetadataToFree->allocatedOrNot = false; //Set it to free
              alreadyInsertedOrNot = true;
            }
          }
          else {
            if ((pCurrentStruct->buffer <= metadataToFree.buffer)) {
              //The metadataToFree->buffer is in between the current struct and the next struct, so we insert after current struct
              MyMalloc::metadata* pOldNext = pCurrentStruct->next;
              if (pOldNext != nullptr) {
                pOldNext->prev = pMetadataToFree;
              }
              pCurrentStruct->next = pMetadataToFree;

              //connect the current struct to the doubly linked list
              if (pMetadataToFree != pCurrentStruct) {
                //In the off chance that this IS the new head, we don't want to set the previous to itself
                pMetadataToFree->prev = pCurrentStruct;
              }

              pMetadataToFree->next = pNextStruct;
              pMetadataToFree->allocatedOrNot = false; //Set it to free
              alreadyInsertedOrNot = true;
            }
          }
        }

        //Actual iteration
        pCurrentStruct = pCurrentStruct->next;
        if (pNextStruct) {
          pNextStruct = pNextStruct->next;
        }
      }


      //We want to keep coalescing until we can no longer merge together
      bool allPossibleFreeSpacesCoalesced = false;
      while (!allPossibleFreeSpacesCoalesced) {
        //Since the function returns 0 if no merging happened, keep going if it returns 1
        allPossibleFreeSpacesCoalesced |= !(this->coalesceFreeSpaces());
      }

    }

  public: //TODO: This needs to be private in production, public now for testing code while running
    void PrintMyMallocFreeList() {
      // Loops through linked list
      metadata* pCurrentStruct = this->freeSectionsListHeadPointer;
      while (pCurrentStruct) {
        printf(
          "block: %p\n \t size: %d\n \t next: %p\n \t prev: %p\n \t buffer: %p\n ",
          pCurrentStruct, 
          pCurrentStruct->size, 
          pCurrentStruct->next, 
          pCurrentStruct->prev, 
          pCurrentStruct->buffer
        );

        pCurrentStruct = pCurrentStruct->next; //Go to next
      }
    }

    static void printBlockInfo(metadata* pMetadataBlock) {
      printf(
        "block: %p\n \t size: %d\n \t next: %p\n \t prev: %p\n \t buffer: %p\n \t allocatedOrNot: %s\n",
        pMetadataBlock, 
        pMetadataBlock->size, 
        pMetadataBlock->next, 
        pMetadataBlock->prev, 
        pMetadataBlock->buffer, 
        (pMetadataBlock->allocatedOrNot) ? "true" : "false"
      );
    }

    void PrintAllBlocks() {
      printf("--------------------------------------------------------\n");
      for (metadata* pCurrentBlock = (metadata*)this->pBackingMemory;
        pCurrentBlock < (metadata*)(this->pBackingMemory + sizeof(this->pBackingMemory));
        pCurrentBlock = (metadata*)(pCurrentBlock->buffer + pCurrentBlock->size)) {

        //If the current block is an allocated space (we set both next,prev pointers to NULL in this case)
        if (pCurrentBlock->allocatedOrNot) {
          printf("\033[1;35m\n"); //Set color to magenta (35)
          printBlockInfo(pCurrentBlock);
        }
        else {
          printf("\033[1;36m\n"); //Set color to cyan (36)
          printBlockInfo(pCurrentBlock);
        }
      }
      printf("\033[1;0m\n"); //Reset the color to black
      printf("--------------------------------------------------------\n");
    }
  };
}; // namespace Jaffx