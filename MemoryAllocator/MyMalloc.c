//
// CS252: MyMalloc Project
//
// The current implementation gets memory from the OS
// every time memory is requested and never frees memory.
//
// You will implement the allocator as indicated in the handout.
// 
// Also you will need to add the necessary locking mechanisms to
// support multi-threaded programs.
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include "MyMalloc.h"

static pthread_mutex_t mutex;

const int ArenaSize = 2097152;  //memory data
const int NumberOfFreeLists = 1; 

// Header of an object. Used both when the object is allocated and freed
struct ObjectHeader {
    size_t _objectSize;         // Real size of the object including header & footer.
    int _allocated;             // 1 = yes, 0 = no 2 = sentinel     sentinel for reference to free list
    struct ObjectHeader * _next;       // Points to the next object in the freelist (if free).  point to what exactly?
    struct ObjectHeader * _prev;       // Points to the previous object. point to which part?
};

struct ObjectFooter {
    size_t _objectSize;
    int _allocated;
};

  //STATE of the allocator

  // Size of the heap
  static size_t _heapSize;    //is _heapSize the size of free list?

  // initial memory pool
  static void * _memStart; //??

  // number of chunks request from OS
  static int _numChunks;

  // True if heap has been initialized
  static int _initialized;

  // Verbose mode
  static int _verbose; //what is verbose?

  // # malloc calls
  static int _mallocCalls;

  // # free calls
  static int _freeCalls;

  // # realloc calls
  static int _reallocCalls;
  
  // # realloc calls
  static int _callocCalls;

  // Free list is a sentinel
  static struct ObjectHeader _freeListSentinel; // Sentinel is used to simplify list operations What are the properties of this object?
  static struct ObjectHeader *_freeList; //pointer to freeList


  //FUNCTIONS

  //Initializes the heap
  void initialize();

  // Allocates an object 
  void * allocateObject( size_t size );

  // Frees an object
  void freeObject( void * ptr );

  // Returns the size of an object
  size_t objectSize( void * ptr );

  // At exit handler
  void atExitHandler();

  //Prints the heap size and other information about the allocator
  void print();
  void print_list();

  // Gets memory from the OS
  void * getMemoryFromOS( size_t size );

  void increaseMallocCalls() { _mallocCalls++; }

  void increaseReallocCalls() { _reallocCalls++; }

  void increaseCallocCalls() { _callocCalls++; }

  void increaseFreeCalls() { _freeCalls++; }

extern void  //what is extern for?
atExitHandlerInC()
{
  atExitHandler();
}

void initialize()
{
  // Environment var VERBOSE prints stats at end and turns on debugging
  // Default is on
  _verbose = 1;
  const char * envverbose = getenv( "MALLOCVERBOSE" );
  if ( envverbose && !strcmp( envverbose, "NO") ) {
    _verbose = 0;
  }

  pthread_mutex_init(&mutex, NULL);
  void * _mem = getMemoryFromOS( ArenaSize + (2*sizeof(struct ObjectHeader)) + (2*sizeof(struct ObjectFooter)) ); //free list is made

  // In verbose mode register also printing statistics at exit
  atexit( atExitHandlerInC );

  //establish fence posts
  struct ObjectFooter * fencepost1 = (struct ObjectFooter *)_mem; //_mem cast and so fencepost1 points to a fencepost footer at the beginning.
  fencepost1->_allocated = 1;
  fencepost1->_objectSize = 123456789;
  char * temp = 
      (char *)_mem + (2*sizeof(struct ObjectFooter)) + sizeof(struct ObjectHeader) + ArenaSize; //_mem cast,pointer arithmetic,temp points to end fencepost header.
  struct ObjectHeader * fencepost2 = (struct ObjectHeader *)temp;//fencepost2 points to end fencepost header
  fencepost2->_allocated = 1; //ensures memory beyond these boundaries not merged.
  fencepost2->_objectSize = 123456789;
  fencepost2->_next = NULL;
  fencepost2->_prev = NULL;

  //initialize the list to point to the _mem
  temp = (char *) _mem + sizeof(struct ObjectFooter); //temp points to header of memory block.
  struct ObjectHeader * currentHeader = (struct ObjectHeader *) temp;//currentHeader points to header of mem block.
  temp = (char *)_mem + sizeof(struct ObjectFooter) + sizeof(struct ObjectHeader) + ArenaSize;
  struct ObjectFooter * currentFooter = (struct ObjectFooter *) temp;//currentFooter points to footer of mem block.
  _freeList = &_freeListSentinel;//_freeList is pointer to ObjectHeader _freeListSentinel. reference to the free list.
  currentHeader->_objectSize = ArenaSize + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter); //2MB
  currentHeader->_allocated = 0; //it is free.
  currentHeader->_next = _freeList;//next pointer of header mem block points to _freelist.
  currentHeader->_prev = _freeList;//prev pointer of header mem block points to _freelist
  currentFooter->_allocated = 0;
  currentFooter->_objectSize = currentHeader->_objectSize;
  _freeList->_prev = currentHeader;
  _freeList->_next = currentHeader; 
  _freeList->_allocated = 2; // sentinel. no coalescing.
  _freeList->_objectSize = 0;
  _memStart = (char*) currentHeader; //where the list starts?
}

void * allocateObject( size_t size )
{
  //Make sure that allocator is initialized
  if ( !_initialized ) {
    _initialized = 1;
    initialize();
  }

  // Add the ObjectHeader/Footer to the size and round the total size up to a multiple of
  // 8 bytes for alignment.
  size_t roundedSize = (size + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter) + 7) & ~7;  //amount of mem requested.

  struct ObjectHeader * ptr = _freeList->_next;
  struct ObjectHeader * lastptr = NULL;
  while (ptr != _freeList) { 
    if (ptr->_objectSize >= roundedSize) {
      if (ptr->_objectSize - roundedSize > 8 + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter)) { //can split block
        int secondsize = ptr->_objectSize - roundedSize;
        ptr->_objectSize = roundedSize; //1st block header size = requested size
        ptr->_allocated = 1; //1st block allocated
        struct ObjectFooter * footer1 = (struct ObjectFooter *)((char *)ptr + roundedSize - sizeof(struct ObjectFooter));
        footer1->_objectSize = roundedSize; //1st block footer size
        footer1->_allocated = 1;
        struct ObjectHeader * header2 = (struct ObjectHeader *)((char *)ptr + roundedSize);
        header2->_objectSize = secondsize; //2nd block header
        header2->_allocated = 0;
        struct ObjectFooter * footer2 = (struct ObjectFooter *)((char *)ptr + roundedSize + secondsize - sizeof(struct ObjectFooter));
        footer2->_objectSize = secondsize; //2nd block footer.
        footer2->_allocated  = 0;
        void * memstart = (void *)((char *)ptr + sizeof(struct ObjectHeader));
        _freeList->_next = header2;
        _freeList->_prev = header2;
        header2->_next = ptr->_next;
        header2->_prev = ptr->_prev;
        pthread_mutex_unlock(&mutex);
        return memstart;
      } else if (ptr->_objectSize == roundedSize) {
        ptr->_objectSize = roundedSize;
        ptr->_allocated = 1;
        struct ObjectFooter * footer1 = (struct ObjectFooter *)((char *)ptr + roundedSize - sizeof(struct ObjectFooter));
        footer1->_objectSize = roundedSize; //1st block footer size
        footer1->_allocated = 1;
        _freeList->_next = _freeList;
        _freeList->_prev = _freeList;
        void * memstart = (void *)((char *)ptr + sizeof(struct ObjectHeader));
        pthread_mutex_unlock(&mutex);
        return memstart;
      }
    }
    lastptr = ptr;
    ptr = ptr->_next;
  }
  void * _mem = getMemoryFromOS( ArenaSize + (2*sizeof(struct ObjectHeader)) + (2*sizeof(struct ObjectFooter)) );
  struct ObjectFooter * fencepost1 = (struct ObjectFooter *)_mem; //_mem cast and so fencepost1 points to a fencepost footer at the beginning.
  fencepost1->_allocated = 1;
  fencepost1->_objectSize = 123456789;
  char * temp = 
      (char *)_mem + (2*sizeof(struct ObjectFooter)) + sizeof(struct ObjectHeader) + ArenaSize; //_mem cast,pointer arithmetic,temp points to end fencepost header.
  struct ObjectHeader * fencepost2 = (struct ObjectHeader *)temp;//fencepost2 points to end fencepost header
  fencepost2->_allocated = 1; //ensures memory beyond these boundaries not merged.
  fencepost2->_objectSize = 123456789;
  fencepost2->_next = NULL;
  fencepost2->_prev = NULL;
  temp = (char *) _mem + sizeof(struct ObjectFooter); //temp points to header of memory block.
  struct ObjectHeader * currentHeader = (struct ObjectHeader *) temp;//currentHeader points to header of mem block.
  temp = (char *)_mem + sizeof(struct ObjectFooter) + sizeof(struct ObjectHeader) + ArenaSize;
  struct ObjectFooter * currentFooter = (struct ObjectFooter *) temp;//currentFooter points to footer of mem block.
  //_freeList = &_freeListSentinel;//_freeList is pointer to ObjectHeader _freeListSentinel. reference to the free list.
  currentHeader->_objectSize = ArenaSize + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter); //2MB
  currentHeader->_allocated = 0; //it is free.
  currentHeader->_next = _freeList;//next pointer of header mem block points to _freelist.
  currentHeader->_prev = _freeList;//prev pointer of header mem block points to _freelist
  currentFooter->_allocated = 0;
  currentFooter->_objectSize = currentHeader->_objectSize;
  lastptr->_next = currentHeader;
  lastptr->_prev = currentHeader;
  ptr = _freeList->_next;
  while (ptr != _freeList) { 
    if (ptr->_objectSize >= roundedSize) {
      //if (ptr->_objectSize - roundedSize > 8 + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter)) { //can split block
        int secondsize = ptr->_objectSize - roundedSize;
        ptr->_objectSize = roundedSize; //1st block header size = requested size
        ptr->_allocated = 1; //1st block allocated
        struct ObjectFooter * footer1 = (struct ObjectFooter *)((char *)ptr + roundedSize - sizeof(struct ObjectFooter));
        footer1->_objectSize = roundedSize; //1st block footer size
        footer1->_allocated = 1;
        struct ObjectHeader * header2 = (struct ObjectHeader *)((char *)ptr + roundedSize);
        header2->_objectSize = secondsize; //2nd block header
        header2->_allocated = 0;
        struct ObjectFooter * footer2 = (struct ObjectFooter *)((char *)ptr + roundedSize + secondsize - sizeof(struct ObjectFooter));
        footer2->_objectSize = secondsize; //2nd block footer.
        footer2->_allocated  = 0;
        void * memstart = (void *)((char *)ptr + sizeof(struct ObjectHeader));
        _freeList->_next = header2;
        _freeList->_prev = header2;
        header2->_next = ptr->_next;
        header2->_prev = ptr->_prev;
        pthread_mutex_unlock(&mutex);
        return memstart;
      //}
    }
    //lastptr = ptr;
    ptr = ptr->_next;
  }
  //_freeList->_prev = currentHeader;
  //_freeList->_next = currentHeader; 
  //_freeList->_allocated = 2; // sentinel. no coalescing.
  //_freeList->_objectSize = 0;
  


  // Naively get memory from the OS every time
  //void * _mem = getMemoryFromOS( roundedSize );

  // Store the size in the header
  //struct ObjectHeader * o = (struct ObjectHeader *) _mem;

  //o->_objectSize = roundedSize;

  //pthread_mutex_unlock(&mutex);

  // Return a pointer to usable memory
  //return (void *) (o + 1);  //why o + 1?
}

void freeObject( void * ptr )
{
  // Add your code here
  //struct ObjectFooter * leftfooter = (struct ObjectFooter *)((char*)ptr - sizeof(struct ObjectHeader) - sizeof(struct ObjectFooter));
  struct ObjectHeader * ptrheader = (struct ObjectHeader *)((char*)ptr - sizeof(struct ObjectHeader));
  struct ObjectFooter * leftfooter = (struct ObjectFooter *)((char*)ptrheader - sizeof(struct ObjectFooter));
  struct ObjectHeader * rightheader = (struct ObjectHeader *)((char*)ptrheader + ptrheader->_objectSize);
  int ptrblocksize = ptrheader->_objectSize;
  //struct ObjectHeader * rightheader = (struct ObjectHeader *)((char*)ptr + ptrblocksize);
  if (leftfooter->_allocated == 0) {
    /*struct ObjectHeader * leftheader = (struct ObjectHeader *)((char*)ptrheader - leftfooter->_objectSize);
    leftheader->_objectSize = leftheader->_objectSize + ptrblocksize - sizeof(struct ObjectHeader) - sizeof(struct ObjectFooter);
    leftheader->_allocated = 0;
    struct ObjectFooter * newleftfooter = (struct ObjectFooter *)((char*)leftheader + leftheader->_objectSize - sizeof(struct ObjectFooter));
    newleftfooter->_allocated = 0;
    newleftfooter->_objectSize = leftheader->_objectSize; */
  } else if (rightheader->_allocated == 0) {
    ptrheader->_objectSize = ptrheader->_objectSize + rightheader->_objectSize;
    ptrheader->_allocated = 0;
    struct ObjectFooter * ptrfooter = (struct ObjectFooter *)((char*)ptrheader + ptrheader->_objectSize - sizeof(struct ObjectFooter));
    ptrfooter->_objectSize = ptrheader->_objectSize;
    ptrfooter->_allocated = 0;
    ptrheader->_next = rightheader->_next;
    ptrheader->_prev = rightheader->_prev;
    _freeList->_next = ptrheader;
    _freeList->_prev = ptrheader;
  } else {
    struct ObjectHeader * currentheader = _freeList->_next;
    struct ObjectHeader * lowest = NULL;
    while (currentheader != _freeList) {
    if (currentheader < ptrheader) {
      lowest = currentheader;
    }
    currentheader = currentheader->_next; 
    }
    if (lowest != NULL) {
      ptrheader->_next = lowest->_next;
      ptrheader->_prev = lowest->_prev;
      lowest->_next = ptrheader;
      lowest->_prev = ptrheader;
      ptrheader->_allocated = 0;
      struct ObjectFooter * ptrfooter = (struct ObjectFooter *)((char*)ptrheader + ptrblocksize - sizeof(struct ObjectFooter));
      ptrfooter->_allocated = 0;
      return;
    }
    ptrheader->_allocated = 0;
    ptrheader->_next = _freeList->_next;
    ptrheader->_prev = _freeList->_prev;
    _freeList->_next = ptrheader;
    _freeList->_prev = ptrheader;
    struct ObjectFooter * ptrfooter = (struct ObjectFooter *)((char*)ptrheader + ptrblocksize - sizeof(struct ObjectFooter));
    ptrfooter->_allocated = 0;
  }
  return;
}

size_t objectSize( void * ptr )
{
  // Return the size of the object pointed by ptr. We assume that ptr is a valid obejct.
  struct ObjectHeader * o =
    (struct ObjectHeader *) ( (char *) ptr - sizeof(struct ObjectHeader) ); 
  // Substract the size of the header
  return o->_objectSize;
}

void print()
{
  printf("\n-------------------\n");

  printf("HeapSize:\t%zd bytes\n", _heapSize );
  printf("# mallocs:\t%d\n", _mallocCalls );
  printf("# reallocs:\t%d\n", _reallocCalls );
  printf("# callocs:\t%d\n", _callocCalls );
  printf("# frees:\t%d\n", _freeCalls );

  printf("\n-------------------\n");
}

void print_list()
{
  printf("FreeList: ");
  if ( !_initialized ) {
    _initialized = 1;
    initialize();
  }
  struct ObjectHeader * ptr = _freeList->_next;
  while(ptr != _freeList){
      long offset = (long)ptr - (long)_memStart;
      printf("[offset:%ld,size:%zd]",offset,ptr->_objectSize);
      ptr = ptr->_next;
      if(ptr != NULL){
          printf("->");
      }
  }
  printf("\n");
}

void * getMemoryFromOS( size_t size )
{
  // Use sbrk() to get memory from OS
  _heapSize += size;
 
  void * _mem = sbrk( size );

  if(!_initialized){
      _memStart = _mem;
  }

  _numChunks++;

  return _mem;
}

void atExitHandler()
{
  // Print statistics when exit
  if ( _verbose ) {
    print();
  }
}

//
// C interface
//

extern void *
malloc(size_t size)
{
  pthread_mutex_lock(&mutex);
  increaseMallocCalls();
  
  return allocateObject( size );
}

extern void
free(void *ptr)
{
  pthread_mutex_lock(&mutex);
  increaseFreeCalls();
  
  if ( ptr == 0 ) {
    // No object to free
    pthread_mutex_unlock(&mutex);
    return;
  }
  
  freeObject( ptr );
}

extern void *
realloc(void *ptr, size_t size)
{
  pthread_mutex_lock(&mutex);
  increaseReallocCalls();
    
  // Allocate new object
  void * newptr = allocateObject( size );

  // Copy old object only if ptr != 0
  if ( ptr != 0 ) {
    
    // copy only the minimum number of bytes
    size_t sizeToCopy =  objectSize( ptr );
    if ( sizeToCopy > size ) {
      sizeToCopy = size;
    }
    
    memcpy( newptr, ptr, sizeToCopy );

    //Free old object
    freeObject( ptr );
  }

  return newptr;
}

extern void *
calloc(size_t nelem, size_t elsize)
{
  pthread_mutex_lock(&mutex);
  increaseCallocCalls();
    
  // calloc allocates and initializes
  size_t size = nelem * elsize;

  void * ptr = allocateObject( size );

  if ( ptr ) {
    // No error
    // Initialize chunk with 0s
    memset( ptr, 0, size );
  }

  return ptr;
}

