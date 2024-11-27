namespace Jaffx {
    //Taken from https://electro-smith.github.io/libDaisy/md_doc_2md_2__a6___getting-_started-_external-_s_d_r_a_m.html
    #ifndef DAISY_SDRAM_BASE_ADDR
    #define DAISY_SDRAM_BASE_ADDR 0xC0000000
    #define DAISY_SDRAM_SIZE 67108864 //64 * 1024 * 1024 = 64 MB
    #endif
    #ifndef byte
    #define byte unsigned char
    #endif
    class MyMalloc {
        /*
        making MyMalloc() a singleton because we only want one dynamic memory manager for the SDRAM throughout
        the lifecycle of the Daisy

        Source: https://stackoverflow.com/questions/1008019/how-do-you-implement-the-singleton-design-pattern
        */
        
    public:
        static MyMalloc& getInstance() {
            static MyMalloc instance;
            return instance;
        }
    private:
        byte* pBackingMemory = (byte*)DAISY_SDRAM_BASE_ADDR;

        //Bookkeeping struct
        typedef struct metadata_stc {
            struct MyMalloc::metadata_stc* next;
            struct MyMalloc::metadata_stc* prev;
            int size;
            bool allocatedOrNot;
            byte* buffer;
        } metadata;
        
        MyMalloc::metadata* freeSectionsListHeadPointer;
        
        //Private constructor
        MyMalloc() {
            //Actually initialize the 24-byte struct at the beginning - careful as this might segfault later when `initialStruct` goes out of scope
            MyMalloc::metadata initialStruct;
            initialStruct.next = nullptr;
            initialStruct.prev = nullptr;
            initialStruct.size = DAISY_SDRAM_SIZE - sizeof(MyMalloc::metadata);
            initialStruct.allocatedOrNot = false;
            initialStruct.buffer = (byte*) (&(this->pBackingMemory[0]) + sizeof(MyMalloc::metadata));

            // Putting initial struct into start of BigBuffer so it is accessible outside this scope and we can avoid segfaults
            storeMetadataStructInBigBuffer(this->pBackingMemory, initialStruct); 

            freeSectionsListHeadPointer = (metadata*)&(this->pBackingMemory[0]);
        }
    public: //deleted functions should be online
        //Have the copy and copy-assignment constructors be disabled so that our singleton can remain as the only instance
        MyMalloc(const MyMalloc&) = delete;
        void operator=(const MyMalloc&) = delete;
        ~MyMalloc() {} //We don't really need a destructor because the memory will be zero-filled on init anyways
    private:
        

        /*******************************Helper functions*************************/

        /**
         * @brief This function returns whether the requested memory pointer is in range of the SDRAM
         * 
         * @param pBufferPos 
         * @return true - Pointer is valid and operations can continue
         * @return false - Pointer is already invalid, break and maybe alert?
         */
        bool pointerInMemoryRange(byte* pBufferPos) {
            return ((pBufferPos >= this->pBackingMemory) && (&(this->pBackingMemory[DAISY_SDRAM_SIZE]) > pBufferPos));
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
            metadata* pMetadataStruct = (metadata*) pBufferPos;
            *pMetadataStruct = inputMetadata; 
        }

        /**
         * @brief Get the `metadata` struct from `BigBuffer` at a given position
         * 
         * @param pBufferPos The position from which to retrieve the `metadata` struct
         * @return The `metadata` struct found at the given position in `BigBuffer`
         */
        metadata getMetadataStructInBigBuffer(byte* pBufferPos) {
            //Check pointer bounds to ensure that the requested read pointer is within the BigBuffer
            if (!pointerInMemoryRange(pBufferPos)) {
                //TODO: Alert or maybe serial print somehow
                return *((metadata*) nullptr); //Hopefully this doesn't violently fail
            }
            metadata pMetadataStruct = *((metadata*) pBufferPos);
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
         * @param requestedSize - The number in bytes of how much data you want allocated in SDRAM
         * @return void* - Pointer to a contiguous array in SDRAM, or `nullptr` if errors
         */
        void* malloc(size_t requestedSize) {
            if (requestedSize <= 0) return nullptr; //Safety check
            //If their requested size is not already divisible by 8, make it so
            int actualSize = (requestedSize % 8) ? 8 - (requestedSize % 8) + requestedSize : requestedSize; //Rounds up to nearest multiple of 8
            if (actualSize == 0) return nullptr;
            
            //Loop through all the values in the list of "free" sections and find the first one that has enough room
            MyMalloc::metadata* freeStruct = this->freeSectionsListHeadPointer;

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
                    if (freeStruct == freeSectionsListHeadPointer) {
                        freeSectionsListHeadPointer = pBufferNewFreeStruct;
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

    };
};