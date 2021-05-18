#pragma once
/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/uio.h> /* struct iovec */
#include <stdio.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <type_traits>
#include <tuple> /* for std::tie */

#include "../Utils/Mempool.hpp"
#include "../Utils/List.hpp"
#include "../Utils/CStr.hpp"
#include "../Utils/Wrappers.hpp"

namespace tnt {

/**
 * Exception safe C++ IO buffer.
 *
 * Allocator requirements (API):
 * allocate() - allocation method, must throw an exception in case it fails.
 * Must return a chunk of memory, which end is aligned by N.
 * In other words, address of a the next after the last byte in a chunk must
 * be round in terms of `% N == 0` (note that N is always a power of 2).
 * Returns chunk of memory of @REAL_SIZE size (which is less or equal to N).
 * deallocate() - release method, takes a pointer to memory allocated
 * by @allocate and frees it. Must not throw an exception.
 * REAL_SIZE - constant determines real size of allocated chunk (excluding
 * overhead taken by allocator).
 */
template <size_t N = 16 * 1024, class allocator = MempoolHolder<N>>
class Buffer
{
private:
	/** =============== Block definition =============== */
	/** Blocks are organized into linked list. */
	struct Block : SingleLink<Block>
	{
		Block(List<Block>& addTo, size_t aid)
			: SingleLink<Block>(addTo, true), id(aid) {}

		/**
		 * Each block is enumerated with incrementally increasing
		 * sequence id.
		 * It is used to compare block's positions in the buffer.
		 */
		size_t id;

		/**
		 * Block itself is allocated in the same chunk so the size
		 * of available memory to keep the data is less than allocator
		 * provides.
		 */
		static constexpr size_t DATA_SIZE = allocator::REAL_SIZE -
			sizeof(SingleLink<Block>) - sizeof(id);
		char data[DATA_SIZE];

		/**
		 * Default new/delete are prohibited.
		 */
		void* operator new(size_t size) = delete;
		void operator delete(void *ptr) = delete;

		char  *begin() { return data; }
		char  *end()   { return data + DATA_SIZE; }
	};

	Block *newBlock(List<Block>& addToList);
	void delBlock(Block *b);
	Block *delBlockAndPrev(Block *b);
	Block *delBlockAndNext(Block *b);

	/**
	 * Allocate a number of blocks to fit required size. This structure is
	 * used to RAII idiom to keep several blocks allocation consistent.
	 */
	struct Blocks : public List<Block> {
		Blocks(Buffer &buf) : m_parent(buf) { }
		~Blocks();
		Buffer &m_parent;
	};
public:
	/** =============== Iterator definition =============== */
	class iterator
		: public std::iterator<std::input_iterator_tag, char>,
		  public SingleLink<iterator>
	{
	public:
		USING_LIST_LINK_METHODS(SingleLink<iterator>);

		explicit iterator(Buffer *buffer);
		iterator(Buffer *buffer, Block *block, char *offset, bool is_head);
		iterator(const iterator &other) = delete;
		iterator(iterator &other);
		iterator(iterator &&other) noexcept = default;

		iterator& operator = (const iterator& other) = delete;
		iterator& operator = (iterator& other);
		iterator& operator = (iterator&& other) noexcept = default;
		iterator& operator ++ ();
		iterator& operator += (size_t step);
		iterator operator + (size_t step);
		const char& operator * () const { return *m_position; }
		char& operator * () { return *m_position; }
		bool operator == (const iterator &a) const;
		bool operator != (const iterator &a) const;
		bool operator  < (const iterator &a) const;
		size_t operator - (const iterator &a) const;
		Block * getBlock() {return m_block;}
		char * getPos() {return m_position;}
		void get(char *buf, size_t size) { m_buffer->get(*this, buf, size); }
	private:
		/** Adjust iterator's position in list of iterators after
		 * moveForward. */
		void adjustPositionForward();
		void moveForward(size_t step);
		void moveBackward(size_t step);

		/** Link to the buffer iterator belongs to. */
		Buffer *m_buffer;
		Block *m_block;
		/** Position inside block. */
		char *m_position;

		friend class Buffer;
	};
	/** =============== Buffer definition =============== */
	/** Copy of any kind is disabled. */
	Buffer(const allocator& all = allocator());
	Buffer(const Buffer& buf) = delete;
	Buffer& operator = (const Buffer& buf) = delete;
	~Buffer();

	/**
	 * Return iterator pointing to the start/end of buffer.
	 */
	iterator begin();
	iterator end();
	/**
	 * Copy content of @a buf (or object @a t) to the buffer's tail
	 * (append data). Can cause reallocation that may throw.
	 */
	void addBack(wrap::Data data);
	template <class T>
	void addBack(const T& t);
	template <char... C>
	void addBack(CStr<C...>);
	void addBack(wrap::Advance advance);

	void dropBack(size_t size);
	void dropFront(size_t size);

	/**
	 * Insert free space of size @a size at the position @a itr pointing to.
	 * Move other iterators and reallocate space on demand. @a size must
	 * be less than block size.
	 */
	void insert(const iterator &itr, size_t size);

	/**
	 * Release memory of size @a size at the position @a itr pointing to.
	 */
	void release(const iterator &itr, size_t size);

	/** Resize memory chunk @a itr pointing to. */
	void resize(const iterator &itr, size_t old_size, size_t new_size);

	/**
	 * Copy content of @a buf of size @a size (or object @a t) to the
	 * position in buffer @a itr pointing to.
	 */
	void set(const iterator &itr, const char *buf, size_t size);
	template <class T>
	void set(const iterator &itr, T&& t);

	/**
	 * Copy content of data iterator pointing to to the buffer @a buf of
	 * size @a size.
	 */
	void get(const iterator& itr, char *buf, size_t size);
	template <class T>
	void get(const iterator& itr, T& t);
	template <class T>
	T get(const iterator& itr);

	/**
	 * Determine whether the buffer has @a size bytes after @ itr.
	 */
	bool has(const iterator& itr, size_t size);
	/**
	 * Drop data till the first existing iterator. In case there's
	 * no iterators erase whole buffer.
	 */
	void flush();

	/**
	 * Move content of buffer starting from position @a itr pointing to
	 * to array of iovecs with size of @a max_size. Each buffer block
	 * is assigned to separate iovec (so at one we copy max @a max_size
	 * blocks).
	 */
	size_t getIOV(const iterator &itr, struct iovec *vecs, size_t max_size);
	size_t getIOV(const iterator &start, const iterator &end,
		      struct iovec *vecs, size_t max_size);

	/** Return true if there's no data in the buffer. */
	bool empty() const { return m_begin == m_end; }

	/** Return 0 if everythng is correct. */
	int debugSelfCheck() const;

	static int blockSize() { return N; }
#ifndef NDEBUG
	/** Print content of buffer to @a output in human-readable format. */
	template<size_t size, class alloc>
	friend std::string dump(Buffer<size, alloc> &buffer);
#endif
private:
	class List<Block> m_blocks;
	/** List of all data iterators created via @a begin method. */
	class List<iterator> m_iterators;
	/** Id generator for the next create block. */
	size_t m_blockId;
	/**
	 * Offset of the data in the first block. Data may start not from
	 * the beginning of the block due to ::dropFront invocation.
	 */
	char *m_begin;
	/** Last block can be partially filled, so store end border as well. */
	char *m_end;

	/** Instance of an allocator. */
	allocator m_all;
};

template <size_t N, class allocator>
typename Buffer<N, allocator>::Block *
Buffer<N, allocator>::newBlock(List<Block>& addToList)
{
	char *ptr = m_all.allocate();
	assert(ptr != nullptr);
	assert((uintptr_t(ptr) + m_all.REAL_SIZE) % N == 0);
	return ::new(ptr) Block(addToList, m_blockId++);
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::delBlock(Block *b)
{
	b->~Block();
	m_all.deallocate(reinterpret_cast<char *>(b));
}

template <size_t N, class allocator>
typename Buffer<N, allocator>::Block *
Buffer<N, allocator>::delBlockAndPrev(Block *b)
{
	Block *tmp = &b->prev();
	delBlock(b);
	--m_blockId;
	return tmp;
}

template <size_t N, class allocator>
typename Buffer<N, allocator>::Block *
Buffer<N, allocator>::delBlockAndNext(Block *b)
{
	Block *tmp = &b->next();
	delBlock(b);
	return tmp;
}

template <size_t N, class allocator>
Buffer<N, allocator>::Blocks::~Blocks()
{
	while (!this->isEmpty()) {
		m_parent.delBlock(&this->first());
		--m_parent.m_blockId;
	}
}

template <size_t N, class allocator>
Buffer<N, allocator>::iterator::iterator(Buffer *buffer)
	: m_buffer(buffer), m_block(nullptr), m_position(nullptr)
{
}

template <size_t N, class allocator>
Buffer<N, allocator>::iterator::iterator(Buffer *buffer, Block *block,
					 char *offset, bool is_head)
	: SingleLink<iterator>(buffer->m_iterators, !is_head),
	  m_buffer(buffer), m_block(block), m_position(offset)
{
}

template <size_t N, class allocator>
Buffer<N, allocator>::iterator::iterator(iterator& other)
	: SingleLink<iterator>(other, false),
	  m_buffer(other.m_buffer), m_block(other.m_block),
	  m_position(other.m_position)
{
}

template <size_t N, class allocator>
typename Buffer<N, allocator>::iterator&
Buffer<N, allocator>::iterator::operator= (iterator& other)
{
	if (this == &other)
		return *this;
	assert(m_buffer == other.m_buffer);
	m_block = other.m_block;
	m_position = other.m_position;
	other.insert(*this);
	return *this;
}

template <size_t N, class allocator>
typename Buffer<N, allocator>::iterator&
Buffer<N, allocator>::iterator::operator++()
{
	moveForward(1);
	adjustPositionForward();
	return *this;
}

template <size_t N, class allocator>
typename Buffer<N, allocator>::iterator&
Buffer<N, allocator>::iterator::operator+=(size_t step)
{
	moveForward(step);
	/* Adjust iterator's position in the list of iterators. */
	adjustPositionForward();
	return *this;
}

template <size_t N, class allocator>
typename Buffer<N, allocator>::iterator
Buffer<N, allocator>::iterator::operator+(size_t step)
{
	iterator res(*this);
	res += step;
	return res;
}

template <size_t N, class allocator>
bool
Buffer<N, allocator>::iterator::operator==(const iterator& a) const
{
	assert(m_buffer == a.m_buffer);
	return m_position == a.m_position;
}

template <size_t N, class allocator>
bool
Buffer<N, allocator>::iterator::operator!=(const iterator& a) const
{
	assert(m_buffer == a.m_buffer);
	return m_position != a.m_position;
}

template <size_t N, class allocator>
bool
Buffer<N, allocator>::iterator::operator<(const iterator& a) const
{
	assert(m_buffer == a.m_buffer);
	return std::tie(m_block->id, m_position) <
	       std::tie(a.m_block->id, a.m_position);
}

template <size_t N, class allocator>
size_t
Buffer<N, allocator>::iterator::operator-(const iterator& a) const
{
	size_t res = (m_block->id - a.m_block->id) * Block::DATA_SIZE;
	res -= a.m_position - a.m_block->begin();
	res += m_position - m_block->begin();
	return res;
}


template <size_t N, class allocator>
void
Buffer<N, allocator>::iterator::adjustPositionForward()
{
	if (isLast() || !(next() < *this))
		return;
	iterator *itr = &next();
	while (!itr->isLast() && itr->next() < *this)
		itr = &itr->next();
	itr->insert(*this);
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::iterator::moveForward(size_t step)
{
	assert(m_block->begin() <= m_position);
	assert(m_block->end() > m_position);
	while (step >= (size_t)(m_block->end() - m_position))
	{
		step -= m_block->end() - m_position;
		m_block = &m_block->next();
		m_position = m_block->begin();
	}
	m_position += step;
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::iterator::moveBackward(size_t step)
{
	assert(m_block->begin() <= m_position);
	assert(m_block->end() > m_position);
	while (step > (size_t)(m_position - m_block->begin())) {
		step -= m_position - m_block->begin();
		m_block = &m_block->prev();
		m_position = m_block->end();
	}
	m_position -= step;
}

template <size_t N, class allocator>
Buffer<N, allocator>::Buffer(const allocator &all) : m_blockId(0), m_all(all)
{
	static_assert((N & (N - 1)) == 0, "N must be power of 2");
	static_assert(allocator::REAL_SIZE % alignof(Block) == 0,
		      "Allocation size must be multiple of 16 bytes");
	static_assert(sizeof(Block) == allocator::REAL_SIZE,
		      "size of buffer block is expected to match with "
			      "allocation size");

	Block *b = newBlock(m_blocks);
	m_begin = m_end = b->data;
}

template <size_t N, class allocator>
Buffer<N, allocator>::~Buffer()
{
	/* Delete blocks and release occupied memory. */
	while (!m_blocks.isEmpty()) {
		delBlock(&m_blocks.first());
	}
}

template <size_t N, class allocator>
typename Buffer<N, allocator>::iterator
Buffer<N, allocator>::begin()
{
	return iterator(this, &m_blocks.first(), m_begin, true);
}

template <size_t N, class allocator>
typename Buffer<N, allocator>::iterator
Buffer<N, allocator>::end()
{
	return iterator(this, &m_blocks.last(), m_end, false);
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::addBack(wrap::Data data)
{
	const char *buf = data.data;
	size_t size = data.size;
	assert(size != 0);

	size_t left_in_block = m_blocks.last().end() - m_end;
	if (left_in_block > size) {
		memcpy(m_end, buf, size);
		m_end += size;
		return;
	}
	char *new_end = m_end;
	Blocks new_blocks(*this);
	do {
		memcpy(new_end, buf, left_in_block);
		Block *b = newBlock(new_blocks);
		new_end = b->begin();
		size -= left_in_block;
		buf += left_in_block;
		left_in_block = Block::DATA_SIZE;
	} while (size >= left_in_block);
	memcpy(new_end, buf, size);
	m_blocks.insert(new_blocks, true);
	m_end = new_end + size;
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::addBack(wrap::Advance advance)
{
	size_t size = advance.size;
	assert(size != 0);

	size_t left_in_block = m_blocks.last().end() - m_end;
	if (left_in_block > size) {
		m_end += size;
		return;
	}
	char *new_end = m_end;
	Blocks new_blocks(*this);
	do {
		Block *b = newBlock(new_blocks);
		new_end = b->begin();
		size -= left_in_block;
		left_in_block = Block::DATA_SIZE;
	} while (size >= left_in_block);
	m_blocks.insert(new_blocks, true);
	m_end = new_end + size;
}

template <size_t N, class allocator>
template <class T>
void
Buffer<N, allocator>::addBack(const T& t)
{
	char data[sizeof(T)];
	memcpy(data, &t, sizeof(T));
	addBack(wrap::Data{data, sizeof(T)});
}

template <size_t N, class allocator>
template <char... C>
void
Buffer<N, allocator>::addBack(CStr<C...>)
{
	if constexpr (CStr<C...>::size != 0) {
		size_t left_in_block = m_blocks.last().end() - m_end;
		if (left_in_block > CStr<C...>::rnd_size) {
			memcpy(m_end, CStr<C...>::data, CStr<C...>::rnd_size);
			m_end += CStr<C...>::size;
		} else {
			addBack(wrap::Data{CStr<C...>::data, CStr<C...>::size});
		}
	}
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::dropBack(size_t size)
{
	assert(size != 0);
	assert(!m_blocks.isEmpty());

	Block *block = &m_blocks.last();
	size_t left_in_block = m_end - block->begin();

	/* Do not delete the block if it is empty after drop. */
	while (size > left_in_block) {
		assert(!m_blocks.isEmpty());
		block = delBlockAndPrev(block);

		/*
		 * Make sure there's no iterators pointing to the block
		 * to be dropped.
		 */
		assert(m_iterators.isEmpty() ||
		       m_iterators.last().m_block != block);

		m_end = block->end();
		size -= left_in_block;
		left_in_block = Block::DATA_SIZE;
	}
	m_end -= size;
#ifndef NDEBUG
	assert(m_end >= block->begin());
	/*
	 * Two sanity checks: there's no iterators pointing to the dropped
	 * part of block; end of buffer does not cross start of buffer.
	 */
	if (!m_iterators.isEmpty() && m_iterators.last().m_block == block)
		assert(m_iterators.last().m_position <= m_end);
	if (&m_blocks.first() == block)
		assert(m_end >= m_begin);
#endif
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::dropFront(size_t size)
{
	assert(size != 0);
	assert(!m_blocks.isEmpty());

	Block *block = &m_blocks.first();
	size_t left_in_block = block->end() - m_begin;

	while (size > left_in_block) {
#ifndef NDEBUG
		/*
		 * Make sure block to be dropped does not have pointing to it
		 * iterators.
		 */
		if (! m_iterators.empty()) {
			assert(m_iterators.first().m_block != block);
		}
#endif
		block = delBlockAndNext(block);
		m_begin = block->begin();
		size -= left_in_block;
		left_in_block = Block::DATA_SIZE;
	}
	m_begin += size;
#ifndef NDEBUG
	assert(m_begin <= block->end());
	if (!m_iterators.isEmpty() && m_iterators.last().m_block == block)
		assert(m_iterators.last().m_position >= m_begin);
	if (&m_blocks.last() == block)
		assert(m_begin <= m_end);
#endif
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::insert(const iterator &itr, size_t size)
{
	//TODO: rewrite without iterators.
	/* Remember last block before extending the buffer. */
	Block *src_block = &m_blocks.last();
	char *src_block_end = m_end;
	addBack(wrap::Advance{size});
	Block *dst_block = &m_blocks.last();
	char *src = nullptr;
	char *dst = nullptr;
	/*
	 * Special treatment for starting block: we should not go over
	 * iterator's position.
	 * TODO: remove this awful define (but it least it works).
	 */
#define src_block_begin ((src_block == itr.m_block) ? itr.m_position : src_block->begin())
	/* Firstly move data in blocks. */
	size_t left_in_dst_block = m_end - dst_block->begin();
	size_t left_in_src_block = src_block_end - src_block_begin;
	if (left_in_dst_block > left_in_src_block) {
		src = src_block_begin;
		dst = m_end - left_in_src_block;
	} else {
		src = src_block_end - left_in_dst_block;
		dst = dst_block->begin();
	}
	assert(dst <= m_end);
	size_t copy_chunk_sz = std::min(left_in_src_block, left_in_dst_block);
	for (;;) {
		/*
		 * During copying data in block may split into two parts
		 * which get in different blocks. So let's use two-step
		 * memcpy of data in source block.
		 */
		assert(dst_block->id > itr.m_block->id || dst >= itr.m_position);
		std::memmove(dst, src, copy_chunk_sz);
		if (left_in_dst_block > left_in_src_block) {
			left_in_dst_block -= copy_chunk_sz;
			if (src_block == itr.m_block)
				break;
			src_block = &src_block->prev();
			src = src_block->end() - left_in_dst_block;
			left_in_src_block = src_block->end() - src_block_begin;
			dst = dst_block->begin();
			copy_chunk_sz = left_in_dst_block;
		} else {
			/* left_in_src_block >= left_in_dst_block */
			left_in_src_block -= copy_chunk_sz;
			dst_block = &dst_block->prev();
			dst = dst_block->end() - left_in_src_block;
			left_in_dst_block = Block::DATA_SIZE;
			src = src_block->begin();
			copy_chunk_sz = left_in_src_block;
		}
	}
	/* Adjust position for copy in the first block. */
	assert(src_block == itr.m_block);
	assert(itr.m_position >= src);
	/* Select all iterators from end until the same position. */
	for (iterator *tmp = &m_iterators.last();
	     *tmp != itr; tmp = &tmp->prev())
		tmp->moveForward(size);
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::release(const iterator &itr, size_t size)
{
	//TODO: rewrite without iterators.
	Block *src_block = itr.m_block;
	Block *dst_block = itr.m_block;
	char *src = itr.m_position;
	char *dst = itr.m_position;
	/* Locate the block to start copying with. */
	size_t step = size;
	assert(src_block->end() >= src);
	while (step >= (size_t)(src_block->end() - src)) {
		step -= src_block->end() - src;
		src_block = &src_block->next();
		src = src_block->begin();
	}
	src += step;
	/* Firstly move data in blocks. */
	size_t left_in_dst_block = dst_block->end() - dst;
	size_t left_in_src_block = src_block->end() - src;
	size_t copy_chunk_sz = std::min(left_in_src_block, left_in_dst_block);
	for (;;) {
		std::memmove(dst, src, copy_chunk_sz);
		if (left_in_dst_block > left_in_src_block) {
			left_in_dst_block -= copy_chunk_sz;
			/*
			 * We don't care if in the last iteration we copy a
			 * little bit more in destination block since
			 * this data anyway will be truncated by ::dropBack()
			 * call in the end of function.
			 */
			if (src_block == &m_blocks.last())
				break;
			src_block = &src_block->next();
			src = src_block->begin();
			left_in_src_block = Block::DATA_SIZE;
			dst += copy_chunk_sz;
			copy_chunk_sz = left_in_dst_block;
		} else {
			/* left_in_src_block >= left_in_dst_block */
			left_in_src_block -= copy_chunk_sz;
			dst_block = &dst_block->next();
			dst = dst_block->begin();
			left_in_dst_block = Block::DATA_SIZE;
			src += copy_chunk_sz;
			copy_chunk_sz = left_in_src_block;
		}
	};

	/* Now adjust iterators' positions. */
	/* Select all iterators from end until the same position. */
	for (iterator *tmp = &m_iterators.last();
	     *tmp != itr; tmp = &tmp->prev())
		tmp->moveBackward(size);

	/* Finally drop unused chunk. */
	dropBack(size);
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::resize(const iterator &itr, size_t size, size_t new_size)
{
	if (new_size > size)
		insert(itr, new_size - size);
	else
		release(itr, size - new_size);
}

template <size_t N, class allocator>
size_t
Buffer<N, allocator>::getIOV(const iterator &itr, struct iovec *vecs,
			     size_t max_size)
{
	return getIOV(itr, end(), vecs, max_size);
}

template <size_t N, class allocator>
size_t
Buffer<N, allocator>::getIOV(const iterator &start, const iterator &end,
			     struct iovec *vecs, size_t max_size)
{
	assert(vecs != NULL);
	assert(start < end || start == end);
	Block *block = start.m_block;
	Block *last_block = end.m_block;
	char *pos = start.m_position;
	size_t vec_cnt = 0;
	for (; vec_cnt < max_size;) {
		struct iovec *vec = &vecs[vec_cnt];
		++vec_cnt;
		vec->iov_base = pos;
		if (block == last_block) {
			vec->iov_len = (size_t) (end.m_position - pos);
			break;
		}
		vec->iov_len = (size_t) (block->end() - pos);
		block = &block->next();
		pos = block->begin();
	}
	return vec_cnt;
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::set(const iterator &itr, const char *buf, size_t size)
{
	Block *block = itr.m_block;
	char *pos = itr.m_position;
	size_t left_in_block = block->end() - pos;
	const char *buf_pos = buf;
	while (size > 0) {
		size_t copy_sz = std::min(size, left_in_block);
		std::memcpy(pos, buf_pos, copy_sz);
		size -= copy_sz;
		buf_pos += copy_sz;
		if (size == 0)
			break;
		block = &block->next();
		pos = (char *)&block->data;
		left_in_block = Block::DATA_SIZE;
	}
}

template <size_t N, class allocator>
template <class T>
void
Buffer<N, allocator>::set(const iterator &itr, T&& t)
{
	/*
	 * Do not even attempt at copying non-standard classes (such as
	 * containing vtabs).
	 */
	static_assert(std::is_standard_layout_v<std::remove_reference_t<T>>,
		      "T is expected to have standard layout");
	size_t t_size = sizeof(t);
	const char *tc = &reinterpret_cast<const char &>(t);
	if (t_size <= (size_t)(itr.m_block->end() - itr.m_position))
		memcpy(itr.m_position, tc, sizeof(t));
	else
		set(itr, tc, t_size);
}

template <size_t N, class allocator>
void
Buffer<N, allocator>::get(const iterator& itr, char *buf, size_t size)
{
	/*
	 * The same implementation as in ::set() method buf vice versa:
	 * buffer and data sources are swapped.
	 */
	struct Block *block = itr.m_block;
	char *pos = itr.m_position;
	size_t left_in_block = block->end() - itr.m_position;
	while (size > 0) {
		size_t copy_sz = std::min(size, left_in_block);
		std::memcpy(buf, pos, copy_sz);
		size -= copy_sz;
		buf += copy_sz;
		if (size == 0)
			break;
		block = &block->next();
		pos = &block->data[0];
		left_in_block = Block::DATA_SIZE;
	}
}

template <size_t N, class allocator>
template <class T>
void
Buffer<N, allocator>::get(const iterator& itr, T& t)
{
	static_assert(std::is_standard_layout_v<std::remove_reference_t<T>>,
		      "T is expected to have standard layout");
	size_t t_size = sizeof(t);
	if (t_size <= (size_t)(itr.m_block->end() - itr.m_position)) {
		memcpy(reinterpret_cast<T*>(&t), itr.m_position, sizeof(T));
	} else {
		get(itr, reinterpret_cast<char *>(&t), t_size);
	}
}

template <size_t N, class allocator>
template <class T>
T
Buffer<N, allocator>::get(const iterator& itr)
{
	static_assert(std::is_standard_layout_v<std::remove_reference_t<T>>,
		      "T is expected to have standard layout");
	T t;
	get(itr, t);
	return t;
}

template <size_t N, class allocator>
bool
Buffer<N, allocator>::has(const iterator& itr, size_t size)
{

	struct Block *block = itr.m_block;
	struct Block *last_block = &m_blocks.last();
	char *pos = itr.m_position;
	if (block != last_block) {
		size_t have = itr.m_block->end() - pos;
		if (size <= have)
			return true;
		size -= have;
		block = &block->next();
		pos = block->begin();
	}
	while (block != last_block) {
		if (size <= Block::DATA_SIZE)
			return true;
		size -= Block::DATA_SIZE;
		block = &block->next();
		pos = block->begin();
	}
	size_t have = m_end - pos ;
	return size <= have;
}

template<size_t N, class allocator>
void
Buffer<N, allocator>::flush()
{
	size_t distance = m_iterators.isEmpty() ?
		end() - begin() : m_iterators.first() - begin();
	if (distance > 0)
		dropFront(distance);
}

template <size_t N, class allocator>
int
Buffer<N, allocator>::debugSelfCheck() const
{
	int res = 0;
	bool first = true;
	size_t expectedId = m_blockId;
	for (const Block& block : m_blocks) {
		if (first)
			first = false;
		else if (block.id != expectedId)
			res |= 1;
		expectedId = block.id + 1;
	}
	if (expectedId != m_blockId)
		res |= 2;

	for (const iterator& itr : m_iterators) {
		if (itr.m_position >= itr.m_block->end())
			res |= 4;
		if (itr.m_position < itr.m_block->begin())
			res |= 8;
	}
	return res;
}

#ifndef NDEBUG
template <size_t N, class allocator>
std::string
dump(Buffer<N, allocator> &buffer)
{
	size_t vec_len = 0;
	size_t IOVEC_MAX = 1024;
	size_t block_cnt = 0;
	struct iovec vec[IOVEC_MAX];
	std::string output;
	for (auto itr = buffer.begin(); itr != buffer.end(); itr += vec_len) {
		size_t vec_cnt = buffer.getIOV(itr, (struct iovec*)&vec, IOVEC_MAX);
		for (size_t i = 0; i < vec_cnt; ++i) {
			output.append("|sz=" + std::to_string(vec[i].iov_len) + "|");
			output.append((const char *) vec[i].iov_base,
				      vec[i].iov_len);
			output.append("|");
			vec_len += vec[i].iov_len;
		}
		block_cnt += vec_cnt;
	}
	output.insert(0, "bcnt=" + std::to_string(block_cnt));
	return output;
}
#endif

} // namespace tnt {
