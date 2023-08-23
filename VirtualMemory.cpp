#include "VirtualMemory.h"
#include "PhysicalMemory.h"

/**
 * This function is responsible for emptying a given frame, i.e filling all the rows with 0's.
 * @param frameAddress The adress of the frame.
 */
void emptyFrame(uint64_t frameAddress) {
    for (int offset = 0; offset < PAGE_SIZE; offset++) {
        PMwrite(frameAddress + offset, 0);
    }
}

void VMinitialize() {
    uint64_t rootFrameAddress = 0;
    emptyFrame(rootFrameAddress);
}

/**
 * This function is responsible for extracting the offset from a memory address.
 * @param virtualAddress The virtual memory address.
 * @return The offset.
 */
uint64_t getOffset(uint64_t virtualAddress){
    uint64_t mask = ((1LL << OFFSET_WIDTH) - 1);
    uint64_t offset = virtualAddress & mask;
    return offset;
}

/**
 * This function computes the minimal cyclical distance between two given pages.
 * @param pageSwappedIn The page we want to swap in the physical memory.
 * @param pageToSwap The page we may swap out of the physical memory.
 * @return The cyclical distance.
 */
uint64_t findCyclicDistance(uint64_t pageSwappedIn, uint64_t pageToSwap){
    long long int firstDistance;
    firstDistance = pageSwappedIn - pageToSwap;
    if (firstDistance < 0) {
        firstDistance = -firstDistance;
    }
    long long int secondDistance = NUM_PAGES - firstDistance;
    return firstDistance > secondDistance ? secondDistance : firstDistance;
}

uint64_t concatenatePath(uint64_t currentPath, uint64_t currentOffset){
    return (currentPath << OFFSET_WIDTH) + currentOffset;
}

// 1010 - parent address
// level = 2
// offset_width = 1
// 1 0 1 0
//

/**
 * This function is responsible for iterating over the table's tree using DFS and looking for a
 * suitable frame, according to the priorities specified in the exercise PDF.
 * @param virtualAddress The virtual address of the page.
 * @param level The level of the tree we are currently in.
 * @param currFrameOffset The current part of the address we want to translate.
 * @param frameIndex The index of the current frame.
 * @param maxFrameIndex The index of the max frame not in use.
 * @param pathToPage Path to potential page to evict.
 */
void findFrame(uint64_t pageNumber, long long int parentAddress, int level,
              std::size_t currFrameOffset, long long int frameIndex, long long int &maxFrameIndex,
              int &maxCyclicalDistance, uint64_t &pageToEvict, uint64_t &parentOfPageToEvict,
              uint64_t &frameToEvict, uint64_t pathToPage, uint64_t &forbiddenFrame,
              word_t &currentFrameIndex, uint64_t addressToAdd, uint64_t &zeroFrameIndex) {
    if(level < TABLES_DEPTH) {
        word_t frameAddress = frameIndex * PAGE_SIZE;
        bool allZeroFrames = true;
        word_t value;
        for (int i = 0; i < PAGE_SIZE; i++){
            PMread(frameAddress + i, &value);
            if (value != 0) {
                allZeroFrames = false;
                if (value > maxFrameIndex && value < NUM_FRAMES) {
                    maxFrameIndex = value;
                }

                uint64_t updatedPathToPage = concatenatePath(pathToPage, i);

                if (level == TABLES_DEPTH - 1) {
                    // we extracted the page number of the page we may want to evict
                    int cyclicDistance = (int)findCyclicDistance(pageNumber, updatedPathToPage);
                    if (cyclicDistance > maxCyclicalDistance) {
                        maxCyclicalDistance = cyclicDistance;
                        pageToEvict = updatedPathToPage;
                        parentOfPageToEvict = frameIndex;
                        frameToEvict = value;
                    }
                }

                findFrame(pageNumber, frameAddress + i, level + 1,
                          currFrameOffset, value, maxFrameIndex, maxCyclicalDistance,
                          pageToEvict, parentOfPageToEvict, frameToEvict, updatedPathToPage,
                          forbiddenFrame, currentFrameIndex, addressToAdd, zeroFrameIndex);
                if (maxFrameIndex == -1) { // A zero frame was found
                    return;
                }
            }
        }
        // If the frames rows are all 0's, and it's the first empty frame we encounter
        if (allZeroFrames && maxFrameIndex != -1 && (uint64_t)frameIndex != forbiddenFrame) {
            currentFrameIndex = (word_t)frameIndex;
            maxFrameIndex = -1;
            zeroFrameIndex = frameIndex;
            if (parentAddress != -1) { // if frame is not frame 0
                PMwrite(parentAddress, 0);
            }
            // Write to the relevant index and clear the table
            PMwrite(addressToAdd, (word_t)frameIndex);
        }
    }
}

/**
 * This function is responsible for adding the frame/page.
 * @param virtualAddress The virtual address of the page.
 * @param level The level of the tree we are currently in.
 * @param currFrameOffset The current part of the address we want to translate.
 * @param frameIndex The index of the current frame.
 * @param maxFrameIndex The index of the max frame not in use.
 */
void addFrame(uint64_t pageNumber, std::size_t currFrameOffset, int level, uint64_t &forbiddenFrame,
              word_t &currentFrameIndex, uint64_t addressToAddTo) {
    long long int maxFrameIndex = 0;
    int maxCyclicalDistance = -1;

    // May cause problem if access not during the 3rd case
    uint64_t pageToEvict = 0;
    uint64_t parentOfPageToEvict = 0;
    uint64_t frameToEvict = 0;
    uint64_t zeroFrameIndex = -1;

    findFrame(pageNumber, -1, 0, currFrameOffset,0, maxFrameIndex,
              maxCyclicalDistance, pageToEvict, parentOfPageToEvict, frameToEvict, 0, forbiddenFrame,
              currentFrameIndex, addressToAddTo, zeroFrameIndex);
    // If the max index is -1, we already found an empty table, so return
    if(maxFrameIndex == -1){
        // empty frame
        if (level == TABLES_DEPTH -1) {
            PMrestore((uint64_t)zeroFrameIndex, pageNumber);
            // What do we do if we restore a zero frame?
            return;
        }

        else {
            emptyFrame(zeroFrameIndex * PAGE_SIZE);
            forbiddenFrame = zeroFrameIndex;
        }
    }
    // If there is a free frame
    else if (maxFrameIndex + 1 < NUM_FRAMES){
        currentFrameIndex = maxFrameIndex + 1;
        PMwrite(addressToAddTo, maxFrameIndex + 1);
        if(level == TABLES_DEPTH - 1){
            PMrestore(maxFrameIndex + 1, pageNumber);
        }
        else {
            emptyFrame((maxFrameIndex + 1) * PAGE_SIZE);
            forbiddenFrame = maxFrameIndex + 1;
        }
    }
    else { // There are no more unused frames
        currentFrameIndex = (word_t) frameToEvict;
        PMevict(frameToEvict, pageToEvict);
        PMwrite(parentOfPageToEvict * PAGE_SIZE + getOffset(pageToEvict), 0);
        PMwrite(addressToAddTo, (word_t )frameToEvict);
        if(level == TABLES_DEPTH - 1){
            PMrestore(frameToEvict, pageNumber);
        }
        else{
            emptyFrame(frameToEvict * PAGE_SIZE);
            forbiddenFrame = frameToEvict;
        }
    }
}

uint64_t translateVirtualAddress(uint64_t virtualAddress){
    // Find first offset lsb bits which are ones
    int bitsToExtract = ((1 << OFFSET_WIDTH) - 1);
    uint64_t pageNumber = virtualAddress >> OFFSET_WIDTH;
    word_t frameIndex = 0; // initialize frame address to point to frame 0
    uint64_t forbiddenFrame = 0;
    for (int level = 0; level < TABLES_DEPTH; level++) {
        // The offset of the frame in level
        std::size_t numBitsToShift = (TABLES_DEPTH - (std::size_t)level) * OFFSET_WIDTH;
        uint64_t currFrameOffset = virtualAddress >> numBitsToShift;
        currFrameOffset = currFrameOffset & bitsToExtract;
        uint64_t addressToAddTo = frameIndex * PAGE_SIZE + currFrameOffset;
        PMread(frameIndex * PAGE_SIZE + currFrameOffset, &frameIndex);
        if (frameIndex == 0) { // There is no child frame
            addFrame(pageNumber, currFrameOffset, level, forbiddenFrame, frameIndex, addressToAddTo);
        }
    }
    uint64_t pageOffset = getOffset(virtualAddress);
    return (frameIndex * PAGE_SIZE) + pageOffset;
}

int VMread(uint64_t virtualAddress, word_t* value) {
    if(!value || virtualAddress >= VIRTUAL_MEMORY_SIZE){
        return 0;
    }

    uint64_t physicalAddress = translateVirtualAddress(virtualAddress);
    PMread(physicalAddress, value);
    return 1;
}

int VMwrite(uint64_t virtualAddress, word_t value){
    if(virtualAddress >= VIRTUAL_MEMORY_SIZE){
        return 0;
    }

    uint64_t physicalAddress = translateVirtualAddress(virtualAddress);
    PMwrite(physicalAddress, value);
    return 1;
}