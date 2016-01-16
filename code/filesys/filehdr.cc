// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "filehdr.h"
#include "debug.h"
#include "synchdisk.h"
#include "main.h"


//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::FileHeader
//	There is no need to initialize a fileheader,
//	since all the information should be initialized by Allocate or FetchFrom.
//	The purpose of this function is to keep valgrind happy.
//----------------------------------------------------------------------
FileHeader::FileHeader()
{
	numBytes = -1;
	numSectors = -1;
	memset(dataSectors, -1, sizeof(dataSectors));

    for(int i = 0; i < NumIndirect; i ++) {
        indirectTable[i] = NULL;
    }

    for(int i = 0; i < NumTripleIndirect; i ++) {
        tripleIndirectTable[i] = NULL;
    }
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::~FileHeader
//	Currently, there is not need to do anything in destructor function.
//	However, if you decide to add some "in-core" data in header
//	Always remember to deallocate their space or you will leak memory
//----------------------------------------------------------------------
FileHeader::~FileHeader()
{

    for(int i = 0; i < NumIndirect; i++) {
        if(indirectTable[i])
            delete indirectTable[i];
    }
}

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(PersistentBitmap *freeMap, int fileSize)
{ 
    int indirectSize;
    int offset; // # of Sector left after allocate to direct index.
    int indexNeeds;
    int sectorNeeds;
   
    numBytes = fileSize;
    numSectors  = divRoundUp(fileSize, SectorSize);
    
    
    if (freeMap->NumClear() < numSectors)
	return FALSE;		// not enough space
    sectorNeeds = numSectors <= NumDirect ? numSectors : NumDirect;
    offset = numSectors - sectorNeeds;
    // allocate direct level datasector
    for (int i = 0; i < sectorNeeds; i++) {
	    dataSectors[i] = freeMap->FindAndSet();
        // since we checked that there was enough free space,
	    // we expect this to succeed
	    ASSERT(dataSectors[i] >= 0);
    }


    if(offset > 0) {
        indexNeeds  = divRoundUp(offset, NumMaxSect);       // calculate how many indirect index do we need
        indirectSize = indexNeeds <= NumIndirect ? indexNeeds : NumIndirect;
        sectorNeeds = offset; // calculate how many sector we need to allocate
        for(int i = 0; i < indirectSize; i ++) {
            int sectorNum;
            bool result;
            dataSectors[NumDirect + i] = freeMap->FindAndSet();
	        ASSERT(dataSectors[NumDirect + i] >= 0);

            
            sectorNum = NumMaxSect > offset ? offset : NumMaxSect;
            offset -= sectorNum;
            ASSERT(offset >= 0);

            indirectTable[i] = new FileHeader();
            result = indirectTable[i]->AllocateIndirect(freeMap, sectorNum);
            if(!result) return result;
        }
    
    }

    return TRUE;
}

bool
FileHeader::AllocateIndirect(PersistentBitmap *freeMap, int sectorNum)
{
    if (freeMap->NumClear() < sectorNum)
	return FALSE;		// not enough space
    
    numBytes = sectorNum * SectorSize;
    numSectors = sectorNum;

    for(int i = 0; i < sectorNum; i ++) {
        dataSectors[i] = freeMap->FindAndSet();

        ASSERT(dataSectors[i] >= 0);
    }
    return TRUE;
}


//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void 
FileHeader::Deallocate(PersistentBitmap *freeMap)
{
    /*for (int i = 0; i < numSectors; i++) {
	ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
	freeMap->Clear((int) dataSectors[i]);
    }*/
    
    for(int i = 0; i < NumMaxSect; i++) {
        if(indirectTable[i]) {
            for(int j = 0; j < indirectTable[i]->numSectors; j) {
                ASSERT(freeMap->Test((int) indirectTable[i]->dataSectors[i]));
                freeMap->Clear((int) indirectTable[i]->dataSectors[i]);
            }
        }
        if(dataSectors[i] != -1) {
	        ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
	        freeMap->Clear((int) dataSectors[i]);
        }
    }


}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector)
{
    kernel->synchDisk->ReadSector(sector, (char *)this);
    
    for(int i = 0; i < NumIndirect; i++) {
        if(dataSectors[NumDirect + i] == -1) break;
        
        indirectTable[i] = new FileHeader();
        indirectTable[i]->FetchFromIndirect(dataSectors[NumDirect + i]);
    }
}

void
FileHeader::FetchFromIndirect(int sector)
{
    kernel->synchDisk->ReadSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector)
{
    char buf[SectorSize];
    //memcpy(&buf, (char *)this, sizeof(buf));
    //kernel->synchDisk->WriteSector(sector, buf); 
    kernel->synchDisk->WriteSector(sector, (char*) this); 
    for(int i = 0; i < NumIndirect; i ++) {
        if(indirectTable[i]) {
            indirectTable[i]->WriteBackIndirect(dataSectors[i + NumDirect]); 
        }
    }
}

void
FileHeader::WriteBackIndirect(int sector)
{
    char buf[SectorSize];
    memcpy(&buf, (char *)this, sizeof(buf));
    //kernel->synchDisk->WriteSector(sector, buf); 
    kernel->synchDisk->WriteSector(sector, (char*) this); 
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::ByteToSector(int offset)
{
    int position = offset / SectorSize;
    if(position < NumDirect)
        return(dataSectors[position]);
    
    position -= NumDirect;
    
    if(position >= 0) {
        int index = position / NumMaxSect;
        int p = position % NumMaxSect;
        return(indirectTable[index]->dataSectors[p]);
    }
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void
FileHeader::Print()
{
    int i, j, k;
    char *data = new char[SectorSize];

    printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
    //for (i = 0; i < numSectors; i++)
    for (i = 0; i < 16; i++)
	printf("%d ", dataSectors[i]);
    printf("\nFile contents:\n");
    //for (i = k = 0; i < numSectors; i++) {
    for (i = k = 0; i < 16; i++) {
	kernel->synchDisk->ReadSector(dataSectors[i], data);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
	    if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
		printf("%c", data[j]);
            else
		printf("\\%x", (unsigned char)data[j]);
	}
        printf("\n"); 
    }
    delete [] data;
}
