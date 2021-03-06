// filesys.cc 
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk 
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them 
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
#ifndef FILESYS_STUB

#include "copyright.h"
#include "debug.h"
#include "disk.h"
#include "pbitmap.h"
#include "directory.h"
#include "filehdr.h"
#include "filesys.h"

// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known 
//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).  
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{ 
    DEBUG(dbgFile, "Initializing the file system.");
    if (format) {
        PersistentBitmap *freeMap = new PersistentBitmap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
		FileHeader *mapHdr = new FileHeader;
		FileHeader *dirHdr = new FileHeader;

        DEBUG(dbgFile, "Formatting the file system.");

		// First, allocate space for FileHeaders for the directory and bitmap
		// (make sure no one else grabs these!)
		freeMap->Mark(FreeMapSector);	    
		freeMap->Mark(DirectorySector);

		// Second, allocate space for the data blocks containing the contents
		// of the directory and bitmap files.  There better be enough space!
		ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
        ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));

		// Flush the bitmap and directory FileHeaders back to disk
		// We need to do this before we can "Open" the file, since open
		// reads the file header off of disk (and currently the disk has garbage
		// on it!).

        DEBUG(dbgFile, "Writing headers back to disk.");
        mapHdr->WriteBack(FreeMapSector);    
        dirHdr->WriteBack(DirectorySector);

		// OK to open the bitmap and directory files now
		// The file system operations assume these two files are left open
		// while Nachos is running.

        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
     
		// Once we have the files "open", we can write the initial version
		// of each file back to disk.  The directory at this point is completely
		// empty; but the bitmap has been changed to reflect the fact that
		// sectors on the disk have been allocated for the file headers and
		// to hold the file data for the directory and bitmap.

        DEBUG(dbgFile, "Writing bitmap and directory back to disk.");
		freeMap->WriteBack(freeMapFile);	 // flush changes to disk
		directory->WriteBack(directoryFile);

		if (debug->IsEnabled('f')) {
			freeMap->Print();
			directory->Print();
        }
        delete freeMap; 
		delete directory; 
		delete mapHdr; 
		delete dirHdr;
    } else {
		// if we are not formatting the disk, just open the files representing
		// the bitmap and directory; these are left open while Nachos is running
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileSystem::~FileSystem
//----------------------------------------------------------------------
FileSystem::~FileSystem()
{
	delete freeMapFile;
	delete directoryFile;
}

//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk 
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file 
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

int
FileSystem::Create(char *name, int initialSize, bool isDir)
{
    Directory *rootDirectory = new Directory(NumDirEntries);
    Directory *targetDirectory;
    OpenFile *targetFile;
    PersistentBitmap *freeMap;
    FileHeader *hdr;
    char BasedPath[MAX_PATH_LEN + 1];
    char act_name[10];
    int success, sector;
    int size = initialSize;
    DEBUG(dbgFile, "Creating file " << name << " size " << initialSize);


    if(isDir) size = DirectoryFileSize;

    rootDirectory->FetchFrom(directoryFile);
    
    ExtractBasePath(BasedPath, act_name, name);
    sector = rootDirectory->SearchPath(BasedPath, 0); // find the sector number of directory.

    if(sector == -1) {
        return 0;
    }
    
    // open and fetch target dir from disk
    targetFile = new OpenFile(sector);
    targetDirectory = new Directory(NumDirEntries);
    targetDirectory->FetchFrom(targetFile);
    
    
    if (targetDirectory->Find(act_name) != -1) {
      success = 0;			// file is already in directory
    }else {	

        freeMap = new PersistentBitmap(freeMapFile,NumSectors);
        sector = freeMap->FindAndSet();	// find a sector to hold the file header
    	if (sector == -1) 		
            success = 0;		// no free block for file header 
        else if (!targetDirectory->Add(act_name, sector, isDir))
            success = 0;	// no space in directory
	    else {
    	    hdr = new FileHeader;
	        if (!hdr->Allocate(freeMap, size))
            	success = FALSE;	// no space on disk for data
	        else {	
	    	    success = 1;
		        // everthing worked, flush all changes back to disk

                hdr->WriteBack(sector); 		
    	    	targetDirectory->WriteBack(targetFile);
    	    	freeMap->WriteBack(freeMapFile);
                    
                if(isDir) {
                    // if it is a directory, need to write directory info back to it.
                    delete targetFile;
                    delete targetDirectory;
                    targetFile = new OpenFile(sector);
                    targetDirectory = new Directory(NumDirEntries);
                    targetDirectory->WriteBack(targetFile);
                }
	        }
            delete hdr;
	    }
        delete freeMap;
    }

    delete rootDirectory;
    delete targetFile;
    delete targetDirectory;
    return success;
}

//----------------------------------------------------------------------
// FileSystem::Open
// 	Open a file for reading and writing.  
//	To open a file:
//	  Find the location of the file's header, using the directory 
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

OpenFile *
FileSystem::Open(char *name)
{ 
    Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
    int sector;
    
    DEBUG(dbgFile, "Opening file" << name);
    directory->FetchFrom(directoryFile);
    //sector = directory->Find(name); 
    sector = directory->SearchPath(name, 0);
    
    if (sector >= 0) 		
	openFile = new OpenFile(sector);	// name was found in directory 
    delete directory;
    return openFile;				// return NULL if not found
}

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool
FileSystem::Remove(char *name, bool recur)
{ 
    Directory *baseDirectory;
    Directory *targetDirectory;
    OpenFile  * baseDir;
    OpenFile  * tarDir;
    char BasePath[MAX_PATH_LEN + 1];
    char filename[FileNameMaxLen + 1];
    PersistentBitmap *freeMap;
    FileHeader *fileHdr;
    int sector;
   
    OPENDIR(baseDirectory, directoryFile);
    ExtractBasePath(BasePath, filename, name);
    sector = baseDirectory->SearchPath(BasePath, 0);

    delete baseDirectory;

    if (sector == -1) {
       return FALSE;			 // file directory not found 
    }
    
    baseDir = new OpenFile(sector);
    OPENDIR(baseDirectory, baseDir);
    sector = baseDirectory->Find(filename); // Find if the file exist
    
    if (sector == -1 || sector == DirectorySector) {
        delete baseDirectory;
        delete baseDir;
        return FALSE;
    }
  
    freeMap = new PersistentBitmap(freeMapFile,NumSectors);
    
    if(recur) {
        tarDir = new OpenFile(sector);
        OPENDIR(targetDirectory, tarDir);
        targetDirectory->Destroy(freeMap, name, tarDir); // Recursively destroy the directory
        delete targetDirectory;
        delete tarDir;
    }
   
    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    fileHdr->Deallocate(freeMap);  		// remove data blocks
    freeMap->Clear(sector);			// remove header block
    baseDirectory->Remove(filename); // remove it from its base

    freeMap->WriteBack(freeMapFile);		// flush to disk
    baseDirectory->WriteBack(baseDir);        // flush to disk
    
    delete baseDirectory;
    delete baseDir;
    delete fileHdr;
    delete freeMap;
    return TRUE;
} 

//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void
FileSystem::List(char *path, bool recur)
{
    Directory *rootDirectory = new Directory(NumDirEntries);
    int sector;
    rootDirectory->FetchFrom(directoryFile);
    sector = rootDirectory->SearchPath(path, 0);

    
    if(sector == DirectorySector)
        rootDirectory->List(NULL, recur);
    else {
        OpenFile* file = new OpenFile(sector);
        Directory *targetDirectory = new Directory(NumDirEntries);
        targetDirectory->FetchFrom(file);
        targetDirectory->List(path, recur);
        delete targetDirectory;
        delete file;
    }


    delete rootDirectory;
}


void
FileSystem::RecursiveList(char *path)
{
    List(path, true);
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void
FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;
    PersistentBitmap *freeMap = new PersistentBitmap(freeMapFile,NumSectors);
    Directory *directory = new Directory(NumDirEntries);

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);
    bitHdr->Print();

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
} 

OpenFileId 
FileSystem::OpenFileForId(char *name)
{
    int fd = 0;
    OpenFile* file = this->Open(name);
    
    while(fd <= MAX_SYS_OPENF) {
        fd ++;
        if(!SysWideOpenFileTable[fd]) {
            SysWideOpenFileTable[fd] = file;
            break;
        }
    }
    ASSERT(fd <= MAX_SYS_OPENF);

    return fd;
}

int 
FileSystem::WriteToFileId(char *buf, int size, OpenFileId id)
{
    OpenFile* file = SysWideOpenFileTable[id];
    return file->Write(buf, size);
}

int 
FileSystem::ReadFromFileId(char *buf, int size, OpenFileId id)
{
    OpenFile* file = SysWideOpenFileTable[id];
    return file->Read(buf, size);
}

int 
FileSystem::CloseFileId(OpenFileId id)
{
    OpenFile* file = SysWideOpenFileTable[id];
    
    if(file == NULL)
        return 0;
    delete file;

    SysWideOpenFileTable[id] = NULL;
    
    return 1;
}

void
FileSystem::ExtractBasePath(char *base, char *name, char *abs)
{
    // Extract base path by searching for the location of last '/'
    int mark;
    int i, j = 0;
    for(i = 0; abs[i]; i++)
        if(abs[i] == '/')
            mark = i;

    for(i = 0; i < mark; i ++)
        base[i] = abs[i];
    base[i] = '\0';

    for(i = mark; abs[i]; i++)
        name[j++] = abs[i];
    name[j] = '\0';
}


#endif // FILESYS_STUB
