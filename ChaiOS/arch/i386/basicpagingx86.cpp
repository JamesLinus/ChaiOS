/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2015 Nathaniel Cleland
Licensed under the ChaiOS License
See License for full details

Project: ChaiOS
File: basicpagingx86.cpp
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\arch\i386\basicpagingx86.cpp
Created by Nathaniel on 31/10/2014 at 15:05

Description: Sets up early paging for kernel and low memory
			 Provides some basic management functions
**********************************************************/
#define CHAIOS_EARLY

#include "basicpaging.h"
#include "stdafx.h"
#include "pagingdefs.h"
#include "stack.h"
#include "vmmngr.h"

//Memory map (paged)
//0-0xfffff: low memory
//0x100000-0x3fffff: kernel & stuff (low mirror)
//0x400000-0x400fff: basic mapping page
//0xC0000000-0xC07fffff: kernel (full mapping)
//0xF5000000-0xF5003fff: basic stack
//0xFFC00000-0xFFFFFFFF: paging structures


#ifdef X86

NO_INLINE(void basic_paging_setup())
{
	//We are currently unpaged
	//create page tables
	uint32_t* pdir = (uint32_t*)(LOADBASE + 0x200000);
	uint32_t* plowent = &pdir[PAGE_TABLE_ENTRIES];
	//8MB for the kernel, allwoing for early memory management
	uint32_t* pkrnent =  &plowent[PAGE_TABLE_ENTRIES];
	uint32_t* pkrnent1 = &pkrnent[PAGE_TABLE_ENTRIES];
	uint32_t* pstkent = &pkrnent1[PAGE_TABLE_ENTRIES];
	uint32_t* stk = (uint32_t*)(STACK_PHY_BASE + STACK_LENGTH);		//16K stack
	//Setup page dir
	for (int n = 0; n < PAGE_TABLE_ENTRIES; n++)
	{
		pdir[n] = 0;
	}
	pdir[PAGE_TABLE_LOW_ENTRY] = (uint32_t)plowent | PAGING_PRESENT | PAGING_WRITE;
	pdir[PAGE_TABLE_KERNEL_ENTRY] = (uint32_t)pkrnent | PAGING_PRESENT | PAGING_WRITE;
	pdir[PAGE_TABLE_KERNEL_ENTRY+1] = (uint32_t)pkrnent1 | PAGING_PRESENT | PAGING_WRITE;
	pdir[PAGE_TABLE_STACK_ENTRY] = (uint32_t)pstkent | PAGING_PRESENT | PAGING_WRITE;
	pdir[PAGE_TABLE_FRACTAL_ENTRY] = (uint32_t)pdir | PAGING_PRESENT | PAGING_WRITE;		//Fractal paging access
	//Setup page tables
	for (int n = 0; n < PAGE_TABLE_ENTRIES; n++)
	{
		plowent[n] = (0 + n*4096) | PAGING_PRESENT | PAGING_WRITE;
		pkrnent[n] = (LOADBASE + n * 4096) | PAGING_PRESENT | PAGING_WRITE;
		pkrnent1[n] = (LOADBASE + PAGESIZE * PAGE_TABLE_ENTRIES + n * 4096) | PAGING_PRESENT | PAGING_WRITE;
		if (n < STACK_LENGTH/0x1000)		//16K stack
		{
			pstkent[n] = (STACK_PHY_BASE + n * 4096) | PAGING_PRESENT | PAGING_WRITE;
		}
		else
		{
			pstkent[n] = 0;
		}
	}
	stk = &stk[-0x40];		//some buffer
	stk[-1] = (uint32_t)MAKE_VIRTUAL_EARLY(RETURN_ADDRESS());
	//now load our paging structures
	WRITE_CR3((size_t)pdir);
	//Load our stack
	//Now load our stack
	((void(*)(void*))MAKE_PHYSICAL_EARLY(setStack))((void*)(KERNEL_STACK_BASE+((size_t)stk - STACK_PHY_BASE)));
	//enable paging
	WRITE_CR0(READ_CR0() | X86_CR0_FLAG_PAGING);
	((void(*)())stk[-1])();		//Instead of return
}

void* basic_paging_create_mapping(void* physaddr)
{
	//We know that the second entry of the low memory page dir is free, use that
	uint32_t* basicmappingpage = (uint32_t*)0x30000;
	uint32_t offset = (uint32_t)physaddr & 0xFFF;
	physaddr = (void*)((uint32_t)physaddr - offset);
	for (int n = 0; n < PAGE_TABLE_ENTRIES; n++)
	{
		basicmappingpage[n] = 0;
	}
	void* mapping = (void*)0x400000;
	MMU_PD(mapping)[MMU_PD_INDEX(mapping)] = (uint32_t)basicmappingpage | PAGING_PRESENT | PAGING_WRITE;
	//basicmappingpage[MMU_PT_INDEX(mapping)] = (uint32_t)physaddr | PAGING_PRESENT | PAGING_WRITE;
	MMU_PT(mapping)[MMU_PT_INDEX(mapping)] = (uint32_t)physaddr | PAGING_PRESENT | PAGING_WRITE;
	INVLPG(mapping);
	mapping = (void*)(&((uint8_t*)mapping)[offset]);
	return mapping;
}

void basic_paging_close_mapping(void* vitaddr)
{
	MMU_PT(vitaddr)[MMU_PT_INDEX(vitaddr)] = 0;
}

//Vmemmngr wrapper
class CBasicPagingVmemMngr : public CVMemMngr
{
public:
	CBasicPagingVmemMngr();
	//Destruction
	virtual void CALLING_CONVENTION destroy(){ ; }
	//API - Initialization
	virtual PVMMAP_TRANSFER CALLING_CONVENTION vmemTransfer();
	virtual bool CALLING_CONVENTION initialize(CVMemMngr* prevImpl) { UNUSED(prevImpl); return true; }
	//API - Runtime
	virtual virtaddr CALLING_CONVENTION allocate(size_t len, const PAGING_ATTRIBUTES& attr) { UNUSED(len); UNUSED(attr);  return NULL; }
	virtual virtaddr CALLING_CONVENTION allocateSinglePage(const PAGING_ATTRIBUTES& attr) { UNUSED(attr); return NULL; }
	virtual void CALLING_CONVENTION free(virtaddr addr, size_t len, bool bFreePhys) { UNUSED(addr); UNUSED(len); UNUSED(bFreePhys); }
	virtual void CALLING_CONVENTION freeSinglePage(virtaddr addr, bool bFreePhys) { UNUSED(addr); UNUSED(bFreePhys); }
	virtual physaddr CALLING_CONVENTION getPhysicalAddress(virtaddr vAddr) { UNUSED(vAddr); return NULL; }
	virtual virtaddr CALLING_CONVENTION mapPhysicalAddress(physaddr phAddr, size_t len, const PAGING_ATTRIBUTES& attrib) { UNUSED(phAddr); UNUSED(len); UNUSED(attrib); return NULL; }
	virtual virtaddr CALLING_CONVENTION mapSinglePhysicalPage(physaddr phAddr, const PAGING_ATTRIBUTES& attrib) { UNUSED(phAddr); UNUSED(attrib); return NULL; }
	virtual virtaddr CALLING_CONVENTION allocateAt(virtaddr allocLoc, size_t len, const PAGING_ATTRIBUTES& attrib) { UNUSED(allocLoc); UNUSED(len); UNUSED(attrib); return NULL; }
	virtual virtaddr CALLING_CONVENTION allocateSingleAt(virtaddr allocLoc, const PAGING_ATTRIBUTES& attrib) { UNUSED(allocLoc); UNUSED(attrib); return NULL; }
	virtual virtaddr CALLING_CONVENTION mapPhysicalAddressAt(virtaddr allocLoc, physaddr phAddr, size_t len, const PAGING_ATTRIBUTES& attrib) { UNUSED(allocLoc); UNUSED(phAddr); UNUSED(len); UNUSED(attrib); return NULL; }
	virtual virtaddr CALLING_CONVENTION mapSinglePhysicalAddressAt(virtaddr allocLoc, physaddr phAddr, const PAGING_ATTRIBUTES& attrib) { UNUSED(allocLoc); UNUSED(phAddr); UNUSED(attrib); return NULL; }
	virtual size_t CALLING_CONVENTION createNewAddressSpace() { return 0; }
	virtual size_t CALLING_CONVENTION loadAddressSpace(size_t space) { return 0; }
private:
	VMMAP_TRANSFER m_vmemtransfer;
};

CBasicPagingVmemMngr::CBasicPagingVmemMngr()
{
	m_vmemtransfer.pagingrecursiveslot = PAGE_TABLE_FRACTAL_ENTRY;
	m_vmemtransfer.pagingtoplevel = NULL;
}

CVMemMngr::PVMMAP_TRANSFER CBasicPagingVmemMngr::vmemTransfer()
{
	return &m_vmemtransfer;
}

static CBasicPagingVmemMngr VMemMngr;

void* basic_paging_getVmemClass()
{
	return reinterpret_cast<void*>(&VMemMngr);
}


#endif
