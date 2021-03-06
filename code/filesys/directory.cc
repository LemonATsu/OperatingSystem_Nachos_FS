// directory.cc 
//	Routines to manage a directory of file names.
//
//	The directory is a table of fixed length entries; each
//	entry represents a single file, and contains the file name,
//	and the location of the file header on disk.  The fixed size
//	of each directory entry means that we have the restriction
//	of a fixed maximum size for file names.
//
//	The constructor initializes an empty directory of a certain size;
//	we use ReadFrom/WriteBack to fetch the contents of the directory
//	from disk, and to write back any modifications back to disk.
//
//	Also, this implementation has the restriction that the size
//	of the directory cannot expand.  In other words, once all the
//	entries in the directory are used, no more files can be created.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "utility.h"
#include "filehdr.h"
#include "filesys.h"
#include "directory.h"

#define DirectorySector 1

//----------------------------------------------------------------------
// Directory::Directory
// 	Initialize a directory; initially, the directory is completely
//	empty.  If the disk is being formatted, an empty directory
//	is all we need, but otherwise, we need to call FetchFrom in order
//	to initialize it from disk.
//
//	"size" is the number of entries in the directory
//----------------------------------------------------------------------

Directory::Directory(int size)
{
    table = new DirectoryEntry[size];
	
	// MP4 mod tag
	memset(table, 0, sizeof(DirectoryEntry) * size);  // dummy operation to keep valgrind happy
	
    tableSize = size;
    for (int i = 0; i < tableSize; i++)
	table[i].inUse = FALSE;
}

//----------------------------------------------------------------------
// Directory::~Directory
// 	De-allocate directory data structure.
//----------------------------------------------------------------------

Directory::~Directory()
{ 
    delete [] table;
} 

//----------------------------------------------------------------------
// Directory::FetchFrom
// 	Read the contents of the directory from disk.
//
//	"file" -- file containing the directory contents
//----------------------------------------------------------------------

void
Directory::FetchFrom(OpenFile *file)
{
    (void) file->ReadAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
}

//----------------------------------------------------------------------
// Directory::WriteBack
// 	Write any modifications to the directory back to disk
//
//	"file" -- file to contain the new directory contents
//----------------------------------------------------------------------

void
Directory::WriteBack(OpenFile *file)
{
    (void) file->WriteAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
}

//----------------------------------------------------------------------
// Directory::FindIndex
// 	Look up file name in directory, and return its location in the table of
//	directory entries.  Return -1 if the name isn't in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::FindIndex(char *name)
{
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse && !strncmp(table[i].name, name, FileNameMaxLen))
	    return i;
    return -1;		// name not in directory
}

//----------------------------------------------------------------------
// Directory::Find
// 	Look up file name in directory, and return the disk sector number
//	where the file's header is stored. Return -1 if the name isn't 
//	in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::Find(char *name)
{
    int i = FindIndex(name);
    if (i != -1)
	return table[i].sector;
    return -1;
}

int 
Directory::SearchPath(char *name, int offset)
{
    int i, j;
    int sector = -1, flag = 0; // flag to check wheter it has one more level of subdirectory
    char filename[FileNameMaxLen + 1];
    Directory *directory;
    
    // root
    if(name[1] == '\0')
        return DirectorySector;

    // Start string checking at [offset]
    for(i = 1; name[i + offset]; i++) {
        // It has more level
        if(name[i + offset] == '/') {
            for(j = 0; j < i; j++)
                filename[j] = name[j + offset];
            
            filename[j] = '\0';
            flag = 1;
            break;
        }
    }
        
    if(!flag) {
        // If it's in the deepest level. 
        for(j = 0; name[j + offset]; j++)
            filename[j] = name[j + offset];
        filename[j] = '\0';
        // It's the final level. Find if it is at here.
        return Find(filename);
    }
    
    sector = Find(filename);

    if(sector == -1) 
        return sector;
    
    // It's not the deepest level, so it should be a directory, recursively search 
    directory = new Directory(DirectoryFileSize);
    OpenFile* dir = new OpenFile(sector);
    directory->FetchFrom(dir);
    sector = directory->SearchPath(name, i + offset);

    delete directory;
    delete dir;
    
    return sector;


}

//----------------------------------------------------------------------
// Directory::Add
// 	Add a file into the directory.  Return TRUE if successful;
//	return FALSE if the file name is already in the directory, or if
//	the directory is completely full, and has no more space for
//	additional file names.
//
//	"name" -- the name of the file being added
//	"newSector" -- the disk sector containing the added file's header
//----------------------------------------------------------------------

bool
Directory::Add(char *name, int newSector, bool isDir)
{ 
    if (FindIndex(name) != -1)
	return FALSE;
    
    for (int i = 0; i < tableSize; i++)
        if (!table[i].inUse) {
            table[i].inUse = TRUE;
            strncpy(table[i].name, name, FileNameMaxLen); 
            table[i].sector = newSector;
            table[i].isDir = isDir;
        return TRUE;
	}
    return FALSE;	// no space.  Fix when we have extensible files.
}

//----------------------------------------------------------------------
// Directory::Remove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory. 
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------

bool
Directory::Remove(char *name)
{ 
    int i = FindIndex(name);

    if (i == -1)
	return FALSE; 		// name not in directory
    table[i].inUse = FALSE;
    return TRUE;	
}

//----------------------------------------------------------------------
// Directory::List
// 	List all the file names in the directory. 
//----------------------------------------------------------------------

void
Directory::List(char *from, bool recur)
{
   bool free = false;   
   
   if(from == NULL) {
       // It's from root, allocate for it
       from = new char[MAX_PATH_LEN];
       from[0] = '\0';
       free = true;
   }
   
   for (int i = 0; i < tableSize; i++)
	if (table[i].inUse) {
        printf("%s", from);
	    printf("%s ", table[i].name);
    
        if(table[i].isDir)
            printf("D\n");
        else
            printf("F\n");

        // recursively traverse
        if(recur && table[i].isDir) {
            char path[MAX_PATH_LEN];
            
            strncpy(path, from, MAX_PATH_LEN);
            strncat(path, table[i].name, FileNameMaxLen);
            Directory *directory = new Directory(DirectoryFileSize);
            OpenFile *file = new OpenFile(table[i].sector);
            directory->FetchFrom(file);
            directory->List(path,recur);

            delete directory;
            delete file;
        }
    }

    if(free)
        delete[] from;
}

//----------------------------------------------------------------------
// Directory::Print
// 	List all the file names in the directory, their FileHeader locations,
//	and the contents of each file.  For debugging.
//----------------------------------------------------------------------

void
Directory::Print()
{ 
    FileHeader *hdr = new FileHeader;

    printf("Directory contents:\n");
    for (int i = 0; i < tableSize; i++)
	if (table[i].inUse) {
	    printf("Name: %s, Sector: %d\n", table[i].name, table[i].sector);
	    hdr->FetchFrom(table[i].sector);
	    hdr->Print();
	}
    printf("\n");
    delete hdr;
}

bool
Directory::Destroy(PersistentBitmap *freeMap, char *path, OpenFile *file)
{
    FileHeader *fileHdr;

    // Loop through table to remove all file.
    for(int i = 0; i < tableSize; i++) {
        if(table[i].inUse) {
   
            if(table[i].isDir) {
                // It is a directory, remove it recursively
                char tarPath[MAX_PATH_LEN + 1];
                OpenFile *tarDir = new OpenFile(table[i].sector);
                Directory *directory;
                OPENDIR(directory, tarDir);
                
                strncpy(tarPath, path, MAX_PATH_LEN);
                strncat(tarPath, table[i].name, FileNameMaxLen);
                directory->Destroy(freeMap, tarPath, tarDir);                           
                // prevent leak
                delete tarDir;
                delete directory;
            }

            // remove file from table and idsk.
            fileHdr = new FileHeader;
            fileHdr->FetchFrom(table[i].sector);
            fileHdr->Deallocate(freeMap);
            freeMap->Clear(table[i].sector);
            Remove(table[i].name);
           
            delete fileHdr;
        }
    }
    
    // write back all change in the directory
    WriteBack(file);
    return TRUE;
}


