// Suzanna Kisa & Ryan Smith
// CS537-3 Spring 2016
// p5a

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>

// Block 0 is unused.                                                   
// Block 1 is super block. verified -> wsect(1, sb)                       
// Inodes start at block 2.                                             

#define ROOTINO 1  // root i-number                                     
#define BSIZE 512  // block size                                        

typedef unsigned char  uchar;

// File system super block                                    
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks          
  uint ninodes;      // Number of inodes.
};

// On-disk inode structure
#define NDIRECT (12)

#define NINDIRECT (BSIZE / sizeof(uint))

struct dinode {
  short type;      // File type
  short major;     // Major device number (T_DEV only)
  short minor;     // Minor device number (T_DEV only)
  short nlink;     // Number of links to inode in file system
  uint size;       // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};

// Directory is a file containing a sequence 
// of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

#define NINODE       50  // maximum number of active i-nodes

#define IPB           (BSIZE / sizeof(struct dinode))

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block containing bit for block b
#define BBLOCK(b, ninodes) (b/BPB + (ninodes)/IPB + 3)

uchar *inaddr;//[995];
uchar *dirlinks;//[995];
uchar *bitmap;
//unsigned buf[512];
uchar mybitmap[512]={0};
//uint blocksinuse[512]={0};
uchar *usedinodes;//[200]={0};
uchar *dirinodes;//[200]={0};
struct superblock *sb;
uchar *links;

void checkDir(struct dinode* dip, int curr,void* img_ptr, int parent);
void checkAddr(int block);
void setBit(int block);
void checkRefCnts(struct dinode *dip, int inum);
void checkIfRoot(struct dinode *dip);
void checkValidType(struct dinode *dip);
void checkIndirect(int block, void *img_ptr);
void checkIndirect2(int block, void *img_ptr);
void checkDirect(struct dinode *dip);

// xv6 fs img
// similar to vsfs
//  unused  | superblock | inode table | bitmap (data) | data blocks                 
//  (some gaps in here) - inodes have enough info, don't need bitmap       

int
main(int argc, char *argv[])
{ 
  // check if file exists
  struct stat st;
  int result = stat(argv[1], &st);
  if (result == -1){
    fprintf(stderr,"image not found.\n");                                   
    exit(1); 
  }
  
  // try to open image
  int fd = open(argv[1], O_RDONLY);
  if (fd <= -1){
    fprintf(stderr,"ERROR: root directory does not exist.\n");
    exit(1);
   }

  // read first sector
  int rc;
  struct stat sbuf;
  rc = fstat(fd, &sbuf);
  assert(rc == 0);

  // gives ptr to where this file got mapped into your address space
  void *img_ptr = mmap(NULL, sbuf.st_size, PROT_READ,
		       MAP_PRIVATE, fd, 0);
  assert(img_ptr != MAP_FAILED);

  // superblock 
  sb = (struct superblock *)(img_ptr + BSIZE);
  
  links = malloc(sizeof(uchar)*sb->ninodes);
  usedinodes=malloc(sizeof(uchar)*sb->ninodes);
  dirinodes=malloc(sizeof(uchar)*sb->ninodes);
  dirlinks = malloc(sizeof(uchar)*sb->nblocks);
  inaddr = malloc(sizeof(uchar)*sb->nblocks);
  
  int bitblock;
  bitblock = BBLOCK(0, sb->ninodes);
  bitmap = (uchar*)(img_ptr + (bitblock)*BSIZE);
 
  // inodes
  struct dinode *dip = (struct dinode *)(img_ptr + 2*BSIZE);
  int i; // loop through 200 inodes
  for (i = 0; i < sb->ninodes; i++){
    checkValidType(dip);

    if (i == ROOTINO){
      checkIfRoot(dip);
      dirinodes[1]=1;
      links[1]=1;
      checkDir(dip,i,img_ptr,1);
    }

    if (dip->type > 0 && dip->type < 4){
      usedinodes[i]=1;
      
      checkRefCnts(dip,i);
      
      checkIndirect(dip->addrs[NDIRECT],img_ptr);
      if (dip->type != 1){
	checkDirect(dip);
      }
    }
    dip++;
 } // end looping through i inodes

  uchar newbitmap[512];
  int u,v;
  for (u = 0; u<512; u++){
    for (v = 8; v<= 0; v--){

      newbitmap[v]=bitmap[v]%2;
      newbitmap[u] = bitmap[u]/2;
    }
  }
 
  // check inodes
  int y;
  for(y = 1; y < 200; y++){
    if (usedinodes[y]>dirinodes[y]){
      fprintf(stderr,"ERROR: inode marked use but not found in a directory.\n");
      exit(1);
    } else if (usedinodes[y]<dirinodes[y]){
      fprintf(stderr,"ERROR: inode referred to in directory but marked free.\n");
      exit(1);
    }
  }

  // compare bitmap
  int q;
  for(q = 0; q < sb->nblocks/22; q++){
    if (bitmap[q]>mybitmap[q]){
      fprintf(stderr,"ERROR: bitmap marks block in use but it is not in use.\n");
      exit(1);
    } else if (bitmap[q]<mybitmap[q]){
      fprintf(stderr,"ERROR: address used by inode but marked free in bitmap.\n");
      exit(1);
    }   
  }
  return 0;
}

void checkDir(struct dinode* dip, int curr,void* img_ptr, int parent){
  if (dirlinks[curr]!=1 ){
    dirlinks[curr]=1;
   }else {
    if (curr != parent){
      fprintf(stderr,"ERROR: directory appears more than once in file system.\n");
      exit(1);
    }
  }
   
  // let's look inside of the directory
  int p,k,block;
  int dot = 0;
  
  // all other entries
  for (p=0; p<NDIRECT; p++){
    block = dip->addrs[p];
    
    // direct links for directories
    if (block > 28){
      setBit(block);
    }
    if ((block != 0) && ((block < (sb->size - sb->nblocks)) || (block > sb->size))){
      fprintf(stderr,"ERROR: bad address in inode.\n");
      exit(1);
    }
    if (block > 0){
      struct dirent *entry;
      entry = (struct dirent *)(img_ptr + (block*BSIZE));

      for (k=0; k<(512/sizeof(struct dirent)); k++){
	uint inum = entry->inum;
	struct dinode *di = (struct dinode *)(img_ptr + 2*BSIZE + inum*sizeof(struct dinode));
	dirinodes[inum]=1;

      	// look at directories within the directory
	if ((strcmp(entry->name,".")==0) || (strcmp(entry->name,"..")==0)){
	  dot++;
     	  if (strcmp(entry->name,"..")==0){
	    if (curr == ROOTINO && (inum != parent)){
	      fprintf(stderr,"ERROR: root directory does not exist.\n");
	      exit(1);
	    } else if  (inum != parent ){
	      fprintf(stderr,"ERROR: parent directory mismatch.\n");
	      exit(1);
	    }
	  }
	} else {
	  links[inum]+=1;
	} // end checking . and ..
	
	// go back and do the same checks if we're linking out to another directory
	if (di->type == 1 && strcmp(entry->name,".")!=0 && strcmp(entry->name,"..")!=0 && inum != curr){
	  checkDir(di, inum,img_ptr,curr);
	}
	entry++;
      } // end looping through directory
    } // end block

    // Each directory contains . and .. entries
    if (dot != 2){
      fprintf(stderr,"ERROR: directory not properly formatted.\n");
      exit(1);
    }
  } // end checking blocks
}

// set bitmaps to later compare to data bitmap
void setBit(int block){
  int bitblock = BBLOCK(0, sb->ninodes);
  int byte = (block - bitblock - 1) / 8;
  int off = (block - bitblock - 1) % 8;
  mybitmap[byte] |= (1 << (7 - off));
}

// check if reference counts are right
void checkRefCnts(struct dinode *dip, int inum){
  if (dip->nlink != links[inum]){
    fprintf(stderr,"ERROR: bad reference count for file.\n");
    exit(1);
  }
}

// check if the root is in the right spot
void checkIfRoot(struct dinode *dip){
  if (dip->type != 1){
    fprintf(stderr,"ERROR: root directory does not exist.\n");
    exit(1);
  }
}

// check if the inode is of the right type
void checkValidType(struct dinode *dip){
  if (dip->type < 0 || dip->type > 3){
    fprintf(stderr,"ERROR: bad inode.\n");
    exit(1);
  }
}

// set bits for indirect blocks
void checkIndirect2(int block, void *img_ptr){
  if (block > 0){
    setBit(block);
    int indirect = *(int *)(img_ptr + block * BSIZE);
    int w;
    for (w = 0; w < NINDIRECT; w++){
      if (indirect > 0){
	setBit(indirect);
      }
      indirect++;
    }
  }
}

// check addresses for indirect blocks
void checkIndirect(int block, void *img_ptr){
  checkAddr(block);
  checkIndirect2(block,img_ptr);
  if (block > 0){
    int *indirect = (int *)(img_ptr + block*BSIZE);
    int w;
    for (w = 0; w < NINDIRECT; w++){
      if ((indirect[w] != 0)&&((indirect[w] < (sb->size - sb->nblocks)) || (indirect[w] > sb->size))){
	fprintf(stderr,"ERROR: bad address in inode.\n");
	exit(1);
      }
    }
  }
}

// check addresses
void checkAddr(int block){
  if (((block!=0)&& (block < (sb->size - sb->nblocks))) || (block > sb->size )){
      fprintf(stderr,"ERROR: bad address in inode.\n");
      exit(1);
    }
  if (block > 0){
    if (inaddr[block]!=1){
      inaddr[block]=1;
    } else {
      fprintf(stderr,"ERROR: address used more than once.\n");
      exit(1);
    }
  }
}

// check addresses for direct blocks and set bits
void checkDirect(struct dinode *dip){
  int p;  
  for(p = 0; p < NDIRECT; p++){
    int block = (int)dip->addrs[p];
    checkAddr(block);
    if (block > 0){
      setBit(block);
    }
  }
}
