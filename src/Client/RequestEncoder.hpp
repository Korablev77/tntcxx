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
#include <any>
#include <cstdint>
#include <map>

#include "IprotoConstants.hpp"
#include "../mpp/mpp.hpp"
#include "../Utils/Logger.hpp"

enum IteratorType {
	EQ = 0,
	REQ = 1,
	ALL = 2,
	LT = 3,
	LE = 4,
	GE = 5,
	GT = 6,
	BITS_ALL_SET = 7,
	BITS_ANY_SET = 8,
	BITS_ALL_NOT_SET = 9,
	OVERLAPS = 10,
	NEIGHBOR = 11,
};

template<class BUFFER>
using iterator_t = typename BUFFER::iterator;

template<class BUFFER>
class RequestEncoder {
public:
	RequestEncoder(BUFFER &buf) : m_Buf(buf), m_Enc(buf) {};
	~RequestEncoder() { };
	RequestEncoder() = delete;
	RequestEncoder(const RequestEncoder& encoder) = delete;
	RequestEncoder& operator = (const RequestEncoder& encoder) = delete;

	size_t encodePing();
	template <class T>
	size_t encodeInsert(const T &tuple, uint32_t space_id);
	template <class T>
	size_t encodeReplace(const T &tuple, uint32_t space_id);
	template <class T>
	size_t encodeDelete(const T &key, uint32_t space_id, uint32_t index_id);
	template <class K, class T>
	size_t encodeUpdate(const K &key, const T &tuple, uint32_t space_id,
			    uint32_t index_id);
	template <class T, class O>
	size_t encodeUpsert(const T &tuple, const O &opts, uint32_t space_id,
			    uint32_t index_base);
	template <class T>
	size_t encodeSelect(const T& key, uint32_t space_id,
			    uint32_t index_id = 0,
			    uint32_t limit = UINT32_MAX, uint32_t offset = 0,
			    IteratorType iterator = EQ);
	template <class T>
	size_t encodeExecute(const std::string& statement, const T& parameters);
	size_t encodePrepare(const std::string& statement);
	template <class T>
	size_t encodeExecute(unsigned int stmt_id, const T& parameters);
	size_t encodePrepare(const std::string& statement);
	template <class T>
	size_t encodeCall(const std::string &func, const T &args);

	/** Sync value is used as request id. */
	static size_t getSync() { return sync; }
private:
	void encodeHeader(int request);
	BUFFER &m_Buf;
	mpp::Enc<BUFFER> m_Enc;
	inline static ssize_t sync = -1;
	static constexpr size_t PREHEADER_SIZE = 5;
};

template<class BUFFER>
void
RequestEncoder<BUFFER>::encodeHeader(int request)
{
	//TODO: add schema version.
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::SYNC), ++RequestEncoder::sync,
		MPP_AS_CONST(Iproto::REQUEST_TYPE), request)));
}

template<class BUFFER>
size_t
RequestEncoder<BUFFER>::encodePing()
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::PING);
	m_Enc.add(mpp::as_map(std::make_tuple()));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}

template<class BUFFER>
template <class T>
size_t
RequestEncoder<BUFFER>::encodeInsert(const T &tuple, uint32_t space_id)
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::INSERT);
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::SPACE_ID), space_id,
		MPP_AS_CONST(Iproto::TUPLE), tuple)));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}

template<class BUFFER>
template <class T>
size_t
RequestEncoder<BUFFER>::encodeReplace(const T &tuple, uint32_t space_id)
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::REPLACE);
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::SPACE_ID), space_id,
		MPP_AS_CONST(Iproto::TUPLE), tuple)));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}

template<class BUFFER>
template <class T>
size_t
RequestEncoder<BUFFER>::encodeDelete(const T &key, uint32_t space_id,
				     uint32_t index_id)
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::DELETE);
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::SPACE_ID), space_id,
		MPP_AS_CONST(Iproto::INDEX_ID), index_id,
		MPP_AS_CONST(Iproto::KEY), key)));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}

template<class BUFFER>
template <class K, class T>
size_t
RequestEncoder<BUFFER>::encodeUpdate(const K &key, const T &tuple,
				     uint32_t space_id, uint32_t index_id)
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::UPDATE);
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::SPACE_ID), space_id,
		MPP_AS_CONST(Iproto::INDEX_ID), index_id,
		MPP_AS_CONST(Iproto::KEY), key,
		MPP_AS_CONST(Iproto::TUPLE), tuple)));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}

template<class BUFFER>
template <class T, class O>
size_t
RequestEncoder<BUFFER>::encodeUpsert(const T &tuple, const O &ops,
				     uint32_t space_id, uint32_t index_base)
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::UPSERT);
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::SPACE_ID), space_id,
		MPP_AS_CONST(Iproto::INDEX_BASE), index_base,
		MPP_AS_CONST(Iproto::OPS), ops,
		MPP_AS_CONST(Iproto::TUPLE), tuple)));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}

template<class BUFFER>
template <class T>
size_t
RequestEncoder<BUFFER>::encodeSelect(const T &key,
				     uint32_t space_id, uint32_t index_id,
				     uint32_t limit, uint32_t offset,
				     IteratorType iterator)
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::SELECT);
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::SPACE_ID), space_id,
		MPP_AS_CONST(Iproto::INDEX_ID), index_id,
		MPP_AS_CONST(Iproto::LIMIT), limit,
		MPP_AS_CONST(Iproto::OFFSET), offset,
		MPP_AS_CONST(Iproto::ITERATOR), iterator,
		MPP_AS_CONST(Iproto::KEY), key)));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}

template<class BUFFER>
template <class T>
size_t
RequestEncoder<BUFFER>::encodeExecute(const std::string& statement, const T& parameters)
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::EXECUTE);
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::SQL_TEXT), statement,
		MPP_AS_CONST(Iproto::SQL_BIND), parameters,
		MPP_AS_CONST(Iproto::OPTIONS), std::make_tuple())));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}

template<class BUFFER>
template <class T>
size_t
RequestEncoder<BUFFER>::encodeExecute(unsigned int stmt_id, const T& parameters)
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::EXECUTE);
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::STMT_ID), stmt_id,
		MPP_AS_CONST(Iproto::SQL_BIND), parameters,
		MPP_AS_CONST(Iproto::OPTIONS), std::make_tuple())));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}

template<class BUFFER>
size_t
RequestEncoder<BUFFER>::encodePrepare(const std::string& statement)
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::PREPARE);
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::SQL_TEXT), statement)));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}


template<class BUFFER>
template <class T>
size_t
RequestEncoder<BUFFER>::encodeCall(const std::string &func, const T &args)
{
	iterator_t<BUFFER> request_start = m_Buf.end();
	m_Buf.addBack('\xce');
	m_Buf.addBack(uint32_t{0});
	encodeHeader(Iproto::CALL);
	m_Enc.add(mpp::as_map(std::forward_as_tuple(
		MPP_AS_CONST(Iproto::FUNCTION_NAME), func,
		MPP_AS_CONST(Iproto::TUPLE), mpp::as_arr(args))));
	uint32_t request_size = (m_Buf.end() - request_start) - PREHEADER_SIZE;
	m_Buf.set(request_start + 1, __builtin_bswap32(request_size));
	return request_size + PREHEADER_SIZE;
}
