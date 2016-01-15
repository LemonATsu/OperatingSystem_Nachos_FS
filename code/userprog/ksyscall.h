/**************************************************************
 *
 * userprog/ksyscall.h
 *
 * Kernel interface for systemcalls 
 *
 * by Marcus Voelp  (c) Universitaet Karlsruhe
 *
 **************************************************************/

#ifndef __USERPROG_KSYSCALL_H__ 
#define __USERPROG_KSYSCALL_H__ 

#include "kernel.h"

#include "synchconsole.h"


void SysHalt()
{
  kernel->interrupt->Halt();
}

int SysAdd(int op1, int op2)
{
  return op1 + op2;
}

#ifdef FILESYS_STUB
int SysCreate(char *filename)
{
	// return value
	// 1: success
	// 0: failed
	return kernel->interrupt->CreateFile(filename);
}
#endif
int SysCreate(char *filename, int size)
{
	// return value
	// 1: success
	// 0: failed
	return kernel->interrupt->CreateFile(filename, size);
}

OpenFileId SysOpen(char *filename)
{
    return kernel->interrupt->OpenFile(filename);
}

int SysWrite(char *buffer, int size, OpenFileId id) 
{
    return kernel->interrupt->WriteToFileId(buffer, size, id);
}

int SysRead(char *buffer, int size, OpenFileId id)
{
    return kernel->interrupt->ReadFromFileId(buffer, size, id);
}

int SysClose(OpenFileId id)
{
    return kernel->interrupt->CloseFileId(id);
}



#endif /* ! __USERPROG_KSYSCALL_H__ */
