#pragma once

#include <list>
#include <driver_types.h>

namespace Neuro
{
    using namespace std;

    enum EMemStatus
    {
        MEM_STATUS_SUCCESS = 0,
        MEM_STATUS_CUDA_ERROR,
        MEM_STATUS_INVALID_ARGUMENT,
        MEM_STATUS_NOT_INITIALIZED,
        MEM_STATUS_OUT_OF_MEMORY,
        MEM_STATUS_UNKNOWN_ERROR
    };

    enum EMemManagerFlag
    {
        MEM_FLAGS_DEFAULT = 0,       /// Default flags.
        MEM_FLAGS_CANNOT_GROW = 1,   /// Prevent the manager from growing its memory consumption.
        MEM_FLAGS_CANNOT_STEAL = 2,  /// Prevent the manager from stealing memory.
    };

    // A node in the linked list of memory blocks
    class Block
    {
    public:
        Block(char* data, size_t size, Block *next, bool isHead) : m_Data(data) , m_Size(size) , m_Next(next) , m_IsHead(isHead) {}

        inline const char* GetData() const { return m_Data; }
        inline char* GetData() { return m_Data; }

        inline size_t GetSize() const { return m_Size; }

        inline const Block* GetNext() const { return m_Next; }
        inline Block* GetNext() { return m_Next; }

        inline bool IsHead() const { return m_IsHead; }

        inline void SetNext(Block *next) { m_Next = next; }
        inline void SetSize(size_t size) { m_Size = size; }
        inline void SetHeadFlag(bool isHead) { m_IsHead = isHead; }

        /// Debug annotation
        string m_Annotation;

    private:
        /// The pointer to the memory region on the device. 
        char* m_Data;
        /// The size of the memory buffer.
        size_t m_Size;
        /// The prev/next blocks in the linked list of blocks.
        Block* m_Next;        
        /// Is it a head node (i.e. a node obtained from parent->allocate or cudaMalloc).
        bool m_IsHead;
    };

    struct CudaBlock
    {
        void* ptr;
        size_t size;
    };

    struct HostAlloc
    {
        void* ptr;
        size_t size;
        string annotation;
    };

    // Memory manager for GPU device
    class MemoryManager
    {
    public:
        MemoryManager();
        static MemoryManager& Default();

        EMemStatus Reserve(size_t size);

        EMemStatus AllocateDevice(void** ptr, size_t size, const string& annotation = "");
        EMemStatus ReleaseDevice(void* ptr);

        EMemStatus AllocateHost(void** ptr, size_t size, const string& annotation = "");
        EMemStatus ReleaseHost(void* ptr);

        EMemStatus AllocateHostPinned(void** ptr, size_t size, const string& annotation = "");
        EMemStatus ReleaseHostPinned(void* ptr);

        EMemStatus Offload(void* dst, void* src, size_t size, cudaEvent_t memEvent);
        EMemStatus Prefetch(void* dst, void* src, size_t size, cudaEvent_t memEvent);

        EMemStatus WaitForMemEvent(cudaEvent_t memEvent);

        EMemStatus PrintMemoryState(const string& filename) const;
    
    private:
        EMemStatus AllocateBlockUnsafe(Block*& curr, Block*& prev, size_t size);
        EMemStatus ReleaseAllUnsafe();
        EMemStatus ReleaseBlockUnsafe(Block* curr, Block* prev);
        EMemStatus ExtractBlockUnsafe(Block* curr, Block* prev, size_t size, bool stolen);
        EMemStatus FindBestBlockUnsafe(Block*& best, Block*& prev, size_t size);

        EMemStatus AddCudaBlockUnsafe(void* ptr, size_t size);
        EMemStatus RemoveCudaBlockUnsafe(void* ptr);
        
        inline EMemStatus GetUsedMemoryUnsafe(size_t& usedMemory) const;
        inline EMemStatus GetFreeMemoryUnsafe(size_t& freeMemory) const;
        EMemStatus GetMemoryUnsafe(size_t& size, const Block* head) const;
        EMemStatus PrintListUnsafe(FILE *file, const char *name, const Block* head) const;
        EMemStatus PrintHostAllocs(FILE* file, const char* name, const list<HostAlloc>& allocs) const;

        cudaStream_t m_MemoryStream = nullptr;
        bool m_IsStreamBlocking = false;
        Block* m_UsedBlocks = nullptr;
        Block* m_FreeBlocks = nullptr;
        list<CudaBlock> m_CudaBlocks;
        list<HostAlloc> m_HostPinnedAllocations;
        list<HostAlloc> m_HostAllocations;
        size_t m_Size = 0;
        uint32_t m_Flags = MEM_FLAGS_CANNOT_GROW;
        size_t m_AllocatedDeviceMemSize = 0;
        size_t m_AllocatedDeviceMemSizePeak = 0;
        size_t m_AllocatedHostMemSize = 0;
        size_t m_AllocatedHostMemSizePeak = 0;
        size_t m_AllocatedHostPinnedMemSize = 0;
        size_t m_AllocatedHostPinnedMemPeakSize = 0;
    };

    //////////////////////////////////////////////////////////////////////////
    static const char* MemGetErrorString(EMemStatus status)
    {
        switch (status) {
        case MEM_STATUS_SUCCESS: return "MEM_STATUS_SUCCESS";
        case MEM_STATUS_CUDA_ERROR: return "MEM_STATUS_CUDA_ERROR";
        case MEM_STATUS_INVALID_ARGUMENT: return "MEM_STATUS_INVALID_ARGUMENT";
        case MEM_STATUS_NOT_INITIALIZED: return "MEM_STATUS_NOT_INITIALIZED";
        case MEM_STATUS_OUT_OF_MEMORY: return "MEM_STATUS_OUT_OF_MEMORY";
        default: return "MEM_STATUS_UNKNOWN_ERROR";
        }
    }
}
