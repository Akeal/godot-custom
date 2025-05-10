/**************************************************************************/
/*  memory.cpp                                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "memory.h"

#include "core/error/error_macros.h"
#include "core/templates/safe_refcount.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *operator new(size_t p_size, const char *p_description) {
	return Memory::alloc_static(p_size, false);
}

void *operator new(size_t p_size, void *(*p_allocfunc)(size_t p_size)) {
	return p_allocfunc(p_size);
}

#ifdef _MSC_VER
void operator delete(void *p_mem, const char *p_description) {
	CRASH_NOW_MSG("Call to placement delete should not happen.");
}

void operator delete(void *p_mem, void *(*p_allocfunc)(size_t p_size)) {
	CRASH_NOW_MSG("Call to placement delete should not happen.");
}

void operator delete(void *p_mem, void *p_pointer, size_t check, const char *p_description) {
	CRASH_NOW_MSG("Call to placement delete should not happen.");
}
#endif

#ifdef DEBUG_ENABLED
SafeNumeric<uint64_t> Memory::mem_usage;
SafeNumeric<uint64_t> Memory::max_usage;
#endif

SafeNumeric<uint64_t> Memory::alloc_count;

inline bool is_power_of_2(size_t x) { return x && ((x & (x - 1U)) == 0U); }

void *Memory::alloc_aligned_static(size_t p_bytes, size_t p_alignment) {
	DEV_ASSERT(is_power_of_2(p_alignment));

	void *p1, *p2;
	if ((p1 = (void *)malloc(p_bytes + p_alignment - 1 + sizeof(uint32_t))) == nullptr) {
		return nullptr;
	}

	p2 = (void *)(((uintptr_t)p1 + sizeof(uint32_t) + p_alignment - 1) & ~((p_alignment)-1));
	*((uint32_t *)p2 - 1) = (uint32_t)((uintptr_t)p2 - (uintptr_t)p1);
	return p2;
}

void *Memory::realloc_aligned_static(void *p_memory, size_t p_bytes, size_t p_prev_bytes, size_t p_alignment) {
	if (p_memory == nullptr) {
		return alloc_aligned_static(p_bytes, p_alignment);
	}

	void *ret = alloc_aligned_static(p_bytes, p_alignment);
	memcpy(ret, p_memory, p_prev_bytes);
	free_aligned_static(p_memory);
	return ret;
}

void Memory::free_aligned_static(void *p_memory) {
	uint32_t offset = *((uint32_t *)p_memory - 1);
	void *p = (void *)((uint8_t *)p_memory - offset);
	free(p);
}

void *Memory::alloc_static(size_t p_bytes, bool p_pad_align) {
#ifdef DEBUG_ENABLED
	bool prepad = true;
#else
	bool prepad = p_pad_align;
#endif

	void *mem = malloc(p_bytes + (prepad ? DATA_OFFSET : 0));

	ERR_FAIL_NULL_V(mem, nullptr);

	alloc_count.increment();

	if (prepad) {
		uint8_t *s8 = (uint8_t *)mem;

		uint64_t *s = (uint64_t *)(s8 + SIZE_OFFSET);
		*s = p_bytes;

#ifdef DEBUG_ENABLED
		uint64_t new_mem_usage = mem_usage.add(p_bytes);
		max_usage.exchange_if_greater(new_mem_usage);
#endif
		return s8 + DATA_OFFSET;
	} else {
		return mem;
	}
}

void *Memory::realloc_static(void *p_memory, size_t p_bytes, bool p_pad_align) {
	if (p_memory == nullptr) {
		return alloc_static(p_bytes, p_pad_align);
	}

	// Get the address of the memory
	uint8_t *mem = (uint8_t *)p_memory;

#ifdef DEBUG_ENABLED
	bool prepad = true;
#else
	bool prepad = p_pad_align;
#endif

	// If the data should have padding...
	if (prepad) {
		// Move the address to the left DATA_OFFSET amount. DATA_OFFSET being the padding.
		mem -= DATA_OFFSET;
		uint64_t *s = (uint64_t *)(mem + SIZE_OFFSET);

#ifdef DEBUG_ENABLED
		if (p_bytes > *s) {
			uint64_t new_mem_usage = mem_usage.add(p_bytes - *s);
			max_usage.exchange_if_greater(new_mem_usage);
		} else {
			mem_usage.sub(*s - p_bytes);
		}
#endif

		if (p_bytes == 0) {
			// The length of data being stored at this location is 0? Free it!
			free(mem);
			return nullptr;
		} else {
			// There IS some data we want to store at this location.
			// Get a reference (pointer) to the byte length we passed into this method.
			*s = p_bytes;

			// call std::realloc
			// Returns a pointer to the memory pointer of where the block of memory is located.
			// Will either...
			// Return the original location and adjust the allocated memory size to the new size
			// Return a new memory location AND free the memory at the original location
			// Allocates from the position of memory for the length of the requested amount of data we want to store.
			// Mem is pointing at the start of the memory boundary so we need to allocate the length of the actual data PLUS the pre-pad buffer
			mem = (uint8_t *)realloc(mem, p_bytes + DATA_OFFSET);
			ERR_FAIL_NULL_V(mem, nullptr);

			// Update the ORIGINAL memory reference that we passed into this method with the new memory length.
			// Following the call to realloc_static, the memory length variable in the calling method will contain the new value.
			s = (uint64_t *)(mem + SIZE_OFFSET);

			*s = p_bytes;

			// Earlier in the method we moved mem DATA_OFFSET to the left from the original mem address.
			// We only care about the actual data held, not the full set of data that would include the empty pre-pad.
			// So instead of returning the memory address...
			// Return the address of where the data actually starts. The location of the data boundary + DATA_OFFSET
			return mem + DATA_OFFSET;
		}
	} else {
		// If the data does NOT need to be padded...
		// Just resize the data block to the new size to the exact amount
		mem = (uint8_t *)realloc(mem, p_bytes);

		ERR_FAIL_COND_V(mem == nullptr && p_bytes > 0, nullptr);

		// Return the pointer to the memory that was returned by realloc
		return mem;
	}
}

void Memory::free_static(void *p_ptr, bool p_pad_align) {
	ERR_FAIL_NULL(p_ptr);

	// 8 bits of data. It's the pointer address.
	uint8_t *mem = (uint8_t *)p_ptr;

#ifdef DEBUG_ENABLED
	bool prepad = true;
#else
	bool prepad = p_pad_align;
#endif

	// We have one less element allocated.
	alloc_count.decrement();

	if (prepad) {
		// If we pre-padded our data, get the address DATA_OFFSET amount to the left.
		// This will set the pointer to be pointing at the start of the nearest data boundary, before the empty pre-padding.
		mem -= DATA_OFFSET;

#ifdef DEBUG_ENABLED
		uint64_t *s = (uint64_t *)(mem + SIZE_OFFSET);
		mem_usage.sub(*s);
#endif
		// Free the memory at the address
		free(mem);
	} else {
		// If we don't pad the data just free it. Data be where data be.
		free(mem);
	}
}

uint64_t Memory::get_mem_available() {
	return -1; // 0xFFFF...
}

uint64_t Memory::get_mem_usage() {
#ifdef DEBUG_ENABLED
	return mem_usage.get();
#else
	return 0;
#endif
}

uint64_t Memory::get_mem_max_usage() {
#ifdef DEBUG_ENABLED
	return max_usage.get();
#else
	return 0;
#endif
}

_GlobalNil::_GlobalNil() {
	left = this;
	right = this;
	parent = this;
}

_GlobalNil _GlobalNilClass::_nil;
