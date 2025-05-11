/**************************************************************************/
/*  rid_owner.h                                                           */
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

#ifndef RID_OWNER_H
#define RID_OWNER_H

#include "core/os/memory.h"
#include "core/os/mutex.h"
#include "core/string/print_string.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"
#include "core/templates/oa_hash_map.h"
#include "core/templates/rid.h"
#include "core/templates/safe_refcount.h"

#include <stdio.h>
#include <cstdint>
#include <typeinfo>
#include <unordered_map>

// A minimal RID Allocation base class.
// Tracks the current highest unsigned int id that has been issued to a resource.
// Only responsible for incrementing the integer and returning the new value.
// The contents of the RID that it returns is solely the generated integer.
class RID_AllocBase {
	static SafeNumeric<uint64_t> base_id;

protected:
	static RID _make_from_id(uint64_t p_id) {
		RID rid;
		rid._id = p_id;
		return rid;
	}

	static RID _gen_rid() {
		return _make_from_id(_gen_id());
	}

	friend struct VariantUtilityFunctions;

	static uint64_t _gen_id() {
		return base_id.increment();
	}

public:
	virtual ~RID_AllocBase() {}
};

// A more complex class that is responsible for the memory management of tracking where in memory each RID is held (confirm?)
// The RID_Allocator is designed to be defined for a specific data type and whether or not it is thread safe.
// Thread safety is scoped to the full set of chunks the owner is responsible for, utilizing a mutex.
template <typename T, bool THREAD_SAFE = false>
class RID_Alloc : public RID_AllocBase {
	// A chunk is a reference to the exact location in memory where a set of data lies. This is a unit of data we care about keeping track of.
	// Alongside each set of data, a validator is held. This is used to confirm that the RID's validator we're trying to access matches the chunk validator
	struct Chunk {
		T data;
		uint32_t validator; // We compare this validator against free_list_chunks of the same index as the chunk to validate
	};

	// Pointer to a pointer that points to a chunk.
	// This allows us to store each chunk of data in an arbitrary position in memory, effectively creating a collection of mutable size (like a C# List<Chunk>).
	Chunk **chunks = nullptr;

	Chunk getChunks() const {
		return **chunks;
	}

	// Pointer to a pointer that points to a 32-bit integer - the validators.
	uint32_t **free_list_chunks = nullptr;

	// Unordered map of RID to RID_Allocator that we're borrowing from.
	std::unordered_map<uint64_t, RID_Alloc<T, THREAD_SAFE>*> borrowed_rids;

	// Unordered map of RID to RID_Allocator that we're lending to.
	std::unordered_map<uint64_t, RID_Alloc<T, THREAD_SAFE>*> lent_rids;

	// Instantiated in the constructor and never changed.
	// elements_in_chunk = sizeof(T) > p_target_chunk_byte_size ? 1 : (p_target_chunk_byte_size / sizeof(T));
	uint32_t elements_in_chunk;

	// Defines the maximum number of chunks that can be allocated at a given point in time.
	uint32_t max_alloc = 0;

	// Defines the current number of chunks that are allocated at a given point in time.
	uint32_t alloc_count = 0;

	// Defines the absolute maximum number of chunks that can be tracked at any given time.
	// This is required due to unsigned integers being able to only represent up to a maximum amount and is determined when we construct the RID_Alloc.
	// The constructor accepts a maximum number of allowed elements and how many bytes each chunk occupies. Though it seems these values are never used, and rely on the default values of...
	// uint32_t p_target_chunk_byte_size = 65536, uint32_t p_maximum_number_of_elements = 262144
	// 65536 is the maximum value that can be represented by 16 bits -> Q: We're using 65536 as the maximum number of bytes per chunk we allow before only allowing one instance of data per chunk. Why is this our default?
	// 262144... Q: Where does this number come from? ((65536 * 8) / 2) -> (65536 * 4) gets us 262144...
	// The only factor that determines the variance in max chunk limit is determined by the the byte size of T, the size of each unit of data being allocated.
	// So this value is...
	// elements_in_chunk = sizeof(T) > p_target_chunk_byte_size ? 1 : (p_target_chunk_byte_size / sizeof(T));
	// In other words, the elements allowed per chunk is 1 element per chunk if the type of data being stored is greater than the "target" length. Otherwise the elements per chunk however many of the data type fits in the target size.
	// chunk_limit = (p_maximum_number_of_elements / elements_in_chunk) + 1;
	// chunk_limit = (maximum number of elements / number of bytes )
	uint32_t chunk_limit = 0;

	const char *description = nullptr;

	// A mutex to ensure that only one thread interacts with this set of data chunks at a time
	// This is only used if the RID allocator is defined to be thread safe.
	mutable Mutex mutex;

	// The method responsible for creating a new RID and reserving a space in memory for the data it represents.
	_FORCE_INLINE_ RID _allocate_rid() {
		// If this RID allocator is defined to be thread-safe, lock the mutex so nothing else can read or write the data.
		// We need to do this so that a different thread doesn't access the same region of memory/data before the data is ready.
		if constexpr (THREAD_SAFE) {
			mutex.lock();
		}

		// If the number of elements that have been allocated have reached the number of elements we can allocate given the currently reserved number of chunks...
		// We'll need to reserve a new chunk for the new data
		if (alloc_count == max_alloc) {
			// Allocate a new chunk.
			// Update the count of chunks to reflect the new number of elements
			uint32_t chunk_count = alloc_count == 0 ? 0 : (max_alloc / elements_in_chunk);
			// We can't have more than the maximum possible chunks so we want to throw if we get there.
			if (THREAD_SAFE && chunk_count == chunk_limit) {
				// We're having issues but that doesn't mean we need to block everyone else - unlock the mutex so other threads can use it
				mutex.unlock();
				// Error
				if (description != nullptr) {
					ERR_FAIL_V_MSG(RID(), vformat("Element limit for RID of type '%s' reached.", String(description)));
				} else {
					ERR_FAIL_V_MSG(RID(), "Element limit reached.");
				}
			}

			//grow chunks
			if constexpr (!THREAD_SAFE) {
				// chunks = new set of memory, beginning at the pointer to chunk pointer, running the length of the number of chunks (+1 for the new chunk we need to hold) multiplied by the size of a chunk pointer (pointer, not the chunk itself).
				// After this call chunks may or may not be pointing to the same location.
				// If we were passing in a variable for the data length it would hold the new padded length following this call. But we're not doing that atm.
				// Following this call chunks will have space for AT LEAST one more pointer. Maybe more depending on memory offset logic.
				chunks = (Chunk **)memrealloc(chunks, sizeof(Chunk *) * (chunk_count + 1));
			}
			chunks[chunk_count] = (Chunk *)memalloc(sizeof(Chunk) * elements_in_chunk); //but don't initialize
			//grow free lists
			if constexpr (!THREAD_SAFE) {
				// Reallocate the free_list_chunks to be able to hold another chunk of data
				free_list_chunks = (uint32_t **)memrealloc(free_list_chunks, sizeof(uint32_t *) * (chunk_count + 1));
			}
			free_list_chunks[chunk_count] = (uint32_t *)memalloc(sizeof(uint32_t) * elements_in_chunk);

			// Initialize a new chunk of the data type. All to a default or "empty" state
			for (uint32_t i = 0; i < elements_in_chunk; i++) {
				// Don't initialize chunk.
				// empty validator (id)
				chunks[chunk_count][i].validator = 0xFFFFFFFF;
				// free_list_chunks 32-bit integer just contains the element # empty space to have been created. Aka: [1, 2, 3, 4, ..., 445, 446, 447, ..., 999, 1000, 1001]
				free_list_chunks[chunk_count][i] = alloc_count + i;
			}

			// Set the current number of reserved element space to be one chunk larger.
			max_alloc += elements_in_chunk;
		}

		// At this point we know that the number of chunks we have has enough space for the new RID

		// Each chunk of memory is held in a different sequential space in memory.
		// Elements are also allocated to space within those chunks sequentially. Meaning when we pass the boundary of a chunk, we're now in an overall different region of memory.
		// What's the index of the next free space in the chunk collection?
		// We divide the current total number of elements by how many elements we store per chunk.
		// Division truncates the remainder and so this gets us the index of the chunk we want to access.
		// This is a pointer to where the chunk's memory-sequential data is stored.
		// Then we take the current total number of elements modulo elements pre chunk. This gets us the remainder. The index within the chunk.
		uint32_t free_index = free_list_chunks[alloc_count / elements_in_chunk][alloc_count % elements_in_chunk];

		// free_chunk is the division (truncated) of the free index.
		uint32_t free_chunk = free_index / elements_in_chunk;
		// free_element is the modulo (remainder) of the free index.
		uint32_t free_element = free_index % elements_in_chunk;

		// RID_AllocBase, which we inherit from, will give us a new id.
		// gen_id() returns a 64-bit unsigned integer.
		// Mask the returned 64-bit integer so that only the lower 31 bits are kept.
		// Then cast the lower 32 bits to an unsigned 32-bit integer.
		// The 32nd bit MUST be 0 at this point because we masked it. We use that bit to determine if the space occupied by the value is freed or not.
		// This means that the validator is the value of the id and it's determined to be initialized.
		uint32_t validator = (uint32_t)(_gen_id() & 0x7FFFFFFF);
		CRASH_COND_MSG(validator == 0x7FFFFFFF, "Overflow in RID validator");
		// Now convert the 32-bit validator (id) to an unsigned 64-bit integer. We now have 64 bits of data where the upper 32 bits are all 0 and the lower 32 bits are the validator.
		uint64_t id = validator;
		// Shift the bits 32 positions to the left and then write that value back to the id.
		// This means the upper 32 bits now contain the validator (id) + initializer bit, and the bottom 32 bits are 0
		id <<= 32;
		// Bitwise OR and assign back to the id from the free index we determined we will use
		// This means the upper 32 bits are the validator (id) and the lower 32 bits are the index of the memory the data will live in.
		// The 32nd bit should be 
		id |= free_index;

		// For the free space, in the collection of chunks, assign the validator as the 32-bit RID id.
		// When we later want to perform operations on a chunk, we check that the 
		chunks[free_chunk][free_element].validator = validator;
		// Bitwise OR assign. This guarantees that the leftmost (32nd) validator bit is 1. Means the RID is not initialized because we've only allocated space for it.
		chunks[free_chunk][free_element].validator |= 0x80000000; //mark uninitialized bit

		// Increment the number of allocated elements
		alloc_count++;

		if constexpr (THREAD_SAFE) {
			// We're all done here - if this RID owner is thread safe, let other threads go.
			mutex.unlock();
		}

		return _make_from_id(id);
	}

public:
	// Makes a new, empty, RID
	RID make_rid() {
		// Allocate space in memory to store the data for the new RID
		// Returns the RID that refers to that location in memory
		RID rid = _allocate_rid();
		// Initialize an RID that doesn't have an initial value.
		// RID will contain the default value for the rid_owner data type.
		initialize_rid(rid);
		return rid;
	}
	// Makes a new RID containing the data of type T
	RID make_rid(const T &p_value) {
		// Allocate space in memory to store the data for the new RID
		// Returns the RID that refers to that location in memory
		RID rid = _allocate_rid();
		// Initialize an RID that contains the provided value.
		initialize_rid(rid, p_value);
		return rid;
	}

	//allocate but don't initialize, use initialize_rid afterwards
	RID allocate_rid() {
		return _allocate_rid();
	}

	_FORCE_INLINE_ T *get_or_null(const RID &p_rid, bool p_initialize = false) {
		if (p_rid == RID()) {
			return nullptr;
		}

		uint64_t id = p_rid.get_id();

		RID_Alloc<T, THREAD_SAFE>* owner = this;

		if(borrowed_rids.find(id) != borrowed_rids.end()){
			owner = borrowed_rids.at(id);
		}

		uint32_t idx = uint32_t(id & 0xFFFFFFFF);
		if (unlikely(idx >= max_alloc)) {
			return nullptr;
		}

		uint32_t idx_chunk = idx / elements_in_chunk;
		uint32_t idx_element = idx % elements_in_chunk;

		uint32_t validator = uint32_t(id >> 32);

		Chunk &c = owner->chunks[idx_chunk][idx_element];
		if (unlikely(p_initialize)) {
			if (unlikely(!(c.validator & 0x80000000))) {
				ERR_FAIL_V_MSG(nullptr, "Initializing already initialized RID");
			}

			if (unlikely((c.validator & 0x7FFFFFFF) != validator)) {
				ERR_FAIL_V_MSG(nullptr, "Attempting to initialize the wrong RID");
			}

			c.validator &= 0x7FFFFFFF; //initialized

		} else if (unlikely(c.validator != validator)) {
			if ((c.validator & 0x80000000) && c.validator != 0xFFFFFFFF) {
				ERR_FAIL_V_MSG(nullptr, "Attempting to use an uninitialized RID");
			}
			return nullptr;
		}

		T *ptr = &c.data;

		return ptr;
	}
	void initialize_rid(RID p_rid) {
		T *mem = get_or_null(p_rid, true);
		ERR_FAIL_NULL(mem);
		memnew_placement(mem, T);
	}
	void initialize_rid(RID p_rid, const T &p_value) {
		T *mem = get_or_null(p_rid, true);
		ERR_FAIL_NULL(mem);
		memnew_placement(mem, T(p_value));
	}

	// Allows the RID_Owner provided to lend the RID provided from that instance to this instance of RID_Owner
	void borrow_rid(RID_Alloc<T, THREAD_SAFE>* other_rid_alloc, const RID p_rid) {
		if(this == other_rid_alloc){
			ERR_FAIL_MSG("RID_Owner attempted to borrow RID from itself!");
		}

		bool other_owned = other_rid_alloc->owns(p_rid);
		if(!other_owned){
			ERR_FAIL_MSG("Attempted to borrow an RID the does not belong to the provided RID_Owner!");
		}

		uint64_t id = p_rid.get_id();

		if(borrowed_rids.find(id) != borrowed_rids.end()){
			ERR_FAIL_MSG("Attempted to borrow an RID that the RID_Owner was already borrowing!");
		}

		borrowed_rids[id] = other_rid_alloc;
		other_rid_alloc->lent_rids[id] = this;
	}

	_FORCE_INLINE_ bool owns(const RID &p_rid) const {
		if constexpr (THREAD_SAFE) {
			// If we're thread safe, no touchy the data while we read it.
			mutex.lock();
		}

		// We've passed in an RID. Does it belong to this rid_owner instance?
		// Get the 64 bit int id from the RID
		uint64_t id = p_rid.get_id();

		// If we find this id is one this RID_Owner is borrowing, we need to verify that the listed lender owns it and return that result instead.
		if(borrowed_rids.find(id) != borrowed_rids.end()){
			if constexpr (THREAD_SAFE) {
				mutex.unlock();
			}
			RID_Alloc<T, THREAD_SAFE>* owner = borrowed_rids.at(id);
			return owner->owns(p_rid);
		}

		// The index is the id masked with 0xFFFFFFFF.
		// This means we discard the uppermost 16 bytes of data, converting the id into a 32 bit int, from the lower half.
		uint32_t idx = uint32_t(id & 0xFFFFFFFF);
		if (unlikely(idx >= max_alloc)) {
			if constexpr (THREAD_SAFE) {
				mutex.unlock();
			}
			return false;
		}

		// Which chunk index does this RID (element) belong to?
		// By dividing the index by the number of elements per chunk, we truncate the remainder. So we end up with which chunk index the data lives in.
		uint32_t idx_chunk = idx / elements_in_chunk;
		// Then we take the modulo of index and elements. Meaning we only get the remainder - or the index of the element within the chunk.
		uint32_t idx_element = idx % elements_in_chunk;

		// The validator is the upper 32 bits of the int.
		// Perform a rightwise bit shift to get just the upper 32.
		uint32_t validator = uint32_t(id >> 32);

		// We now have both the chunk index AND the validator for the RID of which we're checking ownership.
		// If the validator is 0x7FFFFFFF then it simply hasn't been 
		bool owned = (validator != 0x7FFFFFFF) && (chunks[idx_chunk][idx_element].validator & 0x7FFFFFFF) == validator;

		if constexpr (THREAD_SAFE) {
			// Now other threads can mess with the data. We've read the data.
			mutex.unlock();
		}

		return owned;
	}

	_FORCE_INLINE_ void free(const RID &p_rid) {
		if constexpr (THREAD_SAFE) {
			mutex.lock();
		}

		// Get the full RID which consists of the initialized bit, validator (id), and the chunk element index that holds it
		uint64_t id = p_rid.get_id();

		// If any other RID_Allocator was borrowing this RID, we need to erase it from...
		// This RID_Allocator's map of lent_rids
		// The other RID_Allocator's map of borrowed_rids
		if(lent_rids.find(id) != lent_rids.end()){
			RID_Alloc<T, THREAD_SAFE>* borrower = lent_rids.at(id);
			lent_rids.erase(id);
			borrower->borrowed_rids.erase(id);
		}

		// Mask the id so we just get the validator (id)
		uint32_t idx = uint32_t(id & 0xFFFFFFFF);
		if (unlikely(idx >= max_alloc)) {
			if constexpr (THREAD_SAFE) {
				mutex.unlock();
			}
			ERR_FAIL();
		}

		// Get the chunk this RID used...
		uint32_t idx_chunk = idx / elements_in_chunk;
		// Get the index within the chunk this RID used...
		uint32_t idx_element = idx % elements_in_chunk;

		// Bitshift 32 to the right to get the top 32 bits (validator)
		uint32_t validator = uint32_t(id >> 32);
		
		if (unlikely(chunks[idx_chunk][idx_element].validator & 0x80000000)) {
			if constexpr (THREAD_SAFE) {
				mutex.unlock();
			}
			ERR_FAIL_MSG("Attempted to free an uninitialized or invalid RID");
		} else if (unlikely(chunks[idx_chunk][idx_element].validator != validator)) {
			if constexpr (THREAD_SAFE) {
				mutex.unlock();
			}
			ERR_FAIL();
		}

		// Call the data's destructor
		chunks[idx_chunk][idx_element].data.~T();
		chunks[idx_chunk][idx_element].validator = 0xFFFFFFFF; // go invalid

		alloc_count--;
		free_list_chunks[alloc_count / elements_in_chunk][alloc_count % elements_in_chunk] = idx;

		if constexpr (THREAD_SAFE) {
			mutex.unlock();
		}
	}

	_FORCE_INLINE_ uint32_t get_rid_count() const {
		return alloc_count;
	}

	void get_owned_list(List<RID> *p_owned) const {
		if constexpr (THREAD_SAFE) {
			mutex.lock();
		}
		for (size_t i = 0; i < max_alloc; i++) {
			uint64_t validator = chunks[i / elements_in_chunk][i % elements_in_chunk].validator;
			if (validator != 0xFFFFFFFF) {
				p_owned->push_back(_make_from_id((validator << 32) | i));
			}
		}
		if constexpr (THREAD_SAFE) {
			mutex.unlock();
		}
	}

	//used for fast iteration in the elements or RIDs
	void fill_owned_buffer(RID *p_rid_buffer) const {
		if constexpr (THREAD_SAFE) {
			mutex.lock();
		}
		uint32_t idx = 0;
		for (size_t i = 0; i < max_alloc; i++) {
			uint64_t validator = chunks[i / elements_in_chunk][i % elements_in_chunk].validator;
			if (validator != 0xFFFFFFFF) {
				p_rid_buffer[idx] = _make_from_id((validator << 32) | i);
				idx++;
			}
		}

		if constexpr (THREAD_SAFE) {
			mutex.unlock();
		}
	}

	void set_description(const char *p_descrption) {
		description = p_descrption;
	}

	// Constructor. Set up variables that state how much memory we'll need for our data type, and init double pointers so they can hold pointers.
	RID_Alloc(uint32_t p_target_chunk_byte_size = 65536, uint32_t p_maximum_number_of_elements = 262144) {
		elements_in_chunk = sizeof(T) > p_target_chunk_byte_size ? 1 : (p_target_chunk_byte_size / sizeof(T));
		if constexpr (THREAD_SAFE) {
			chunk_limit = (p_maximum_number_of_elements / elements_in_chunk) + 1;
			chunks = (Chunk **)memalloc(sizeof(Chunk *) * chunk_limit);
			free_list_chunks = (uint32_t **)memalloc(sizeof(uint32_t *) * chunk_limit);
		}
	}

	// Destructor. Free all the memory we're using.
	~RID_Alloc() {
		if (alloc_count) {
			print_error(vformat("ERROR: %d RID allocations of type '%s' were leaked at exit.",
					alloc_count, description ? description : typeid(T).name()));

			for (size_t i = 0; i < max_alloc; i++) {
				uint64_t validator = chunks[i / elements_in_chunk][i % elements_in_chunk].validator;
				if (validator & 0x80000000) {
					continue; //uninitialized
				}
				if (validator != 0xFFFFFFFF) {
					chunks[i / elements_in_chunk][i % elements_in_chunk].data.~T();
				}
			}
		}

		// The number of chunks we have is the number of allocated elements we have, divided my how many elements we can store per chunk
		uint32_t chunk_count = max_alloc / elements_in_chunk;
		// For each chunk...
		for (uint32_t i = 0; i < chunk_count; i++) {
			// chunks[i] is a pointer to a chunk
			memfree(chunks[i]);
			// free_list_chunks[i] is a pointer to a uint
			memfree(free_list_chunks[i]);
		}

		if (chunks) {
			// Free the chunks double pointer
			memfree(chunks);
			// Free the uint double pointer
			memfree(free_list_chunks);
		}
	}
};

template <typename T, bool THREAD_SAFE = false>
class RID_PtrOwner {
	RID_Alloc<T *, THREAD_SAFE> alloc;

public:
	_FORCE_INLINE_ RID make_rid(T *p_ptr) {
		return alloc.make_rid(p_ptr);
	}

	_FORCE_INLINE_ RID allocate_rid() {
		return alloc.allocate_rid();
	}

	_FORCE_INLINE_ void initialize_rid(RID p_rid, T *p_ptr) {
		alloc.initialize_rid(p_rid, p_ptr);
	}

	_FORCE_INLINE_ T *get_or_null(const RID &p_rid) {
		T **ptr = alloc.get_or_null(p_rid);
		if (unlikely(!ptr)) {
			return nullptr;
		}
		return *ptr;
	}
	
	_FORCE_INLINE_ void replace(const RID &p_rid, T *p_new_ptr) {
		T **ptr = alloc.get_or_null(p_rid);
		ERR_FAIL_NULL(ptr);
		*ptr = p_new_ptr;
	}

	_FORCE_INLINE_ bool owns(const RID &p_rid) const {
		return alloc.owns(p_rid);
	}

	_FORCE_INLINE_ void free(const RID &p_rid) {
		alloc.free(p_rid);
	}

	_FORCE_INLINE_ uint32_t get_rid_count() const {
		return alloc.get_rid_count();
	}

	_FORCE_INLINE_ void get_owned_list(List<RID> *p_owned) const {
		return alloc.get_owned_list(p_owned);
	}

	void fill_owned_buffer(RID *p_rid_buffer) const {
		alloc.fill_owned_buffer(p_rid_buffer);
	}

	void set_description(const char *p_descrption) {
		alloc.set_description(p_descrption);
	}

	RID_PtrOwner(uint32_t p_target_chunk_byte_size = 65536, uint32_t p_maximum_number_of_elements = 262144) :
			alloc(p_target_chunk_byte_size, p_maximum_number_of_elements) {}
};

// It appears that RID_Owner is a class designed to wrap around RID_Alloc<T, bool>
// RID_Alloc handles the functional tracking of ownership. We will need to know how it does this in order to properly share.
template <typename T, bool THREAD_SAFE = false>
class RID_Owner {
	RID_Alloc<T, THREAD_SAFE> alloc;
public:
	_FORCE_INLINE_ RID make_rid() {
		return alloc.make_rid();
	}
	_FORCE_INLINE_ RID make_rid(const T &p_ptr) {
		return alloc.make_rid(p_ptr);
	}

	_FORCE_INLINE_ RID allocate_rid() {
		return alloc.allocate_rid();
	}

	_FORCE_INLINE_ void initialize_rid(RID p_rid) {
		alloc.initialize_rid(p_rid);
	}

	_FORCE_INLINE_ void initialize_rid(RID p_rid, const T &p_ptr) {
		alloc.initialize_rid(p_rid, p_ptr);
	}

	_FORCE_INLINE_ T *get_or_null(const RID &p_rid) {
		return alloc.get_or_null(p_rid);
	}

	// Method to allow a different RID_Owner instance access to a given RID
	_FORCE_INLINE_ void borrow_rid(RID_Owner<T, THREAD_SAFE>* p_lending_owner, const RID p_rid) {
		alloc.borrow_rid(&(p_lending_owner->alloc), p_rid);
	}

	// Goal: We need this to return true if an RID is shared with another RID_Owner instance
	_FORCE_INLINE_ bool owns(const RID &p_rid) const {
		return alloc.owns(p_rid);
	}

	// Goal: We need this to properly free the reference of the RID from both this RID_Owner instance AND all instances in which we share this RID
	// Failing to do so properly would result in a memory leak.
	_FORCE_INLINE_ void free(const RID &p_rid) {
		alloc.free(p_rid);
	}

	_FORCE_INLINE_ uint32_t get_rid_count() const {
		return alloc.get_rid_count();
	}

	_FORCE_INLINE_ void get_owned_list(List<RID> *p_owned) const {
		return alloc.get_owned_list(p_owned);
	}
	void fill_owned_buffer(RID *p_rid_buffer) const {
		alloc.fill_owned_buffer(p_rid_buffer);
	}

	void set_description(const char *p_descrption) {
		alloc.set_description(p_descrption);
	}

	RID_Owner(uint32_t p_target_chunk_byte_size = 65536, uint32_t p_maximum_number_of_elements = 262144) :
			alloc(p_target_chunk_byte_size, p_maximum_number_of_elements) {}
};

#endif // RID_OWNER_H
