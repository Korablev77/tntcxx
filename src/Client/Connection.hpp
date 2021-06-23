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

#include "RequestEncoder.hpp"
#include "ResponseDecoder.hpp"

#include "../Utils/rlist.h"
#include "../Utils/Logger.hpp"
#include "../Utils/Wrappers.hpp"

#include <sys/uio.h>

#include <any>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

/** Statistics concerning requests/responses. */
struct ConnectionStat {
	size_t send;
	size_t read;
};

/** rid == request id */
typedef size_t rid_t;

struct ConnectionStatus {
	unsigned is_ready_to_send : 1;
	unsigned is_ready_to_decode : 1;
	unsigned is_send_blocked : 1;
	unsigned is_failed : 1;
};

struct ConnectionError {
	std::string msg;
};

template <class BUFFER, class NetProvider>
class Connector;

/** Each connection is supposed to be bound to a single socket. */
template<class BUFFER, class NetProvider>
class Connection {
public:
	using iterator = typename BUFFER::iterator;

	/**
	 * Public wrappers to access request methods in Tarantool way:
	 * like box.space[space_id].replace() and
	 * box.space[sid].index[iid].select()
	 */
	class Space {
	public:
		Space(Connection<BUFFER, NetProvider> &conn) :
			index(conn, *this), m_Conn(conn) {};
		Space& operator[] (uint32_t id)
		{
			space_id = id;
			return *this;
		}
		template <class T>
		rid_t insert(const T &tuple)
		{
			return m_Conn.insert(tuple, space_id);
		}
		template <class T>
		rid_t replace(const T &tuple)
		{
			return m_Conn.replace(tuple, space_id);
		}
		template <class T>
		rid_t delete_(const T &key, uint32_t index_id = 0)
		{
			return m_Conn.delete_(key, space_id, index_id);
		}
		template <class K, class T>
		rid_t update(const K &key, const T &tuple, uint32_t index_id = 0)
		{
			return m_Conn.update(key, tuple, space_id, index_id);
		}
		template <class T, class O>
		rid_t upsert(const T &tuple, const O &ops, uint32_t index_base = 0)
		{
			return m_Conn.upsert(tuple, ops, space_id, index_base);
		}
		template <class T>
		rid_t select(const T& key, uint32_t index_id = 0,
			     uint32_t limit = UINT32_MAX,
			     uint32_t offset = 0, IteratorType iterator = EQ)
		{
			return m_Conn.select(key, space_id, index_id, limit,
					     offset, iterator);
		}
		class Index {
		public:
			Index(Connection<BUFFER, NetProvider> &conn, Space &space) :
				m_Conn(conn), m_Space(space) {};
			Index& operator[] (uint32_t id)
			{
				index_id = id;
				return *this;
			}
			template <class T>
			rid_t delete_(const T &key)
			{
				return m_Conn.delete_(key, m_Space.space_id,
						      index_id);
			}
			template <class K, class T>
			rid_t update(const K &key, const T &tuple)
			{
				return m_Conn.update(key, tuple,
						     m_Space.space_id, index_id);
			}
			template <class T>
			rid_t select(const T &key,
				     uint32_t limit = UINT32_MAX,
				     uint32_t offset = 0,
				     IteratorType iterator = EQ)
			{
				return m_Conn.select(key, m_Space.space_id,
						     index_id, limit,
						     offset, iterator);
			}
		private:
			Connection<BUFFER, NetProvider> &m_Conn;
			Space &m_Space;
			uint32_t index_id;
		} index;
	private:
		Connection<BUFFER, NetProvider> &m_Conn;
		uint32_t space_id;
	} space;

	Connection(Connector<BUFFER, NetProvider> &connector);
	~Connection();
	Connection(const Connection& connection) = delete;
	Connection& operator = (const Connection& connection) = delete;

	std::optional<Response<BUFFER>> getResponse(rid_t future);
	bool futureIsReady(rid_t future);

	template <class T>
	rid_t call(const std::string &func, const T &args);
	rid_t ping();
	template <class T>
	rid_t execute(const std::string& statement, const T& parameters);

	template <class T>
	rid_t execute(unsigned int stmt_id, const T& parameters);

	rid_t prepare(const std::string& statement);

	void setError(const std::string &msg);
	std::string& getError();
	void reset();

	BUFFER& getInBuf();

#ifndef NDEBUG
	std::string toString();
#endif
	template<class B, class N>
	friend
	struct iovec * outBufferToIOV(Connection<B, N> &conn, size_t *iov_len);

	template<class B, class N>
	friend
	struct iovec * inBufferToIOV(Connection<B, N> &conn, size_t size,
				     size_t *iov_len);

	template<class B, class N>
	friend
	void hasSentBytes(Connection<B, N> &conn, size_t bytes);

	template<class B, class N>
	friend
	void hasNotRecvBytes(Connection<B, N> &conn, size_t bytes);

	template<class B, class N>
	friend
	bool hasDataToSend(Connection<B, N> &conn);

	template<class B, class N>
	friend
	bool hasDataToDecode(Connection<B, N> &conn);

	template<class B, class N>
	friend
	enum DecodeStatus decodeResponse(Connection<B, N> &conn);

	template<class B, class N>
	friend
	int decodeGreeting(Connection<B, N> &conn);

	int socket;
	ConnectionStatus status;
	/** Link Connector::m_ready_to_read */
	struct rlist m_in_read;
	/** Link NetworkProvider::m_ready_to_write */
	struct rlist m_in_write;
	void readyToDecode();
	static constexpr size_t AVAILABLE_IOVEC_COUNT = 32;
	static constexpr size_t GC_STEP_CNT = 5;
private:
	Connector<BUFFER, NetProvider> &m_Connector;

	BUFFER m_InBuf;
	BUFFER m_OutBuf;
	RequestEncoder<BUFFER> m_Encoder;
	ResponseDecoder<BUFFER> m_Decoder;
	iterator m_EndDecoded;
	/**
	 * NetworkProvider can send data up to this iterator (i.e. border
	 * of already encoded requests).
	 */
	iterator m_EndEncoded;
	struct iovec m_IOVecs[AVAILABLE_IOVEC_COUNT];
	ConnectionError m_Error;
	Greeting m_Greeting;

	std::unordered_map<rid_t, Response<BUFFER>> m_Futures;

	template <class T>
	rid_t insert(const T &tuple, uint32_t space_id);
	template <class T>
	rid_t replace(const T &tuple, uint32_t space_id);
	template <class T>
	rid_t delete_(const T &key, uint32_t space_id, uint32_t index_id);
	template <class K, class T>
	rid_t update(const K &key, const T &tuple, uint32_t space_id,
		     uint32_t index_id);
	template <class T, class O>
	rid_t upsert(const T &tuple, const O &ops, uint32_t space_id,
		     uint32_t index_base);
	template <class T>
	rid_t select(const T &key,
		     uint32_t space_id, uint32_t index_id = 0,
		     uint32_t limit = UINT32_MAX,
		     uint32_t offset = 0, IteratorType iterator = EQ);
};

template<class BUFFER, class NetProvider>
Connection<BUFFER, NetProvider>::Connection(Connector<BUFFER, NetProvider> &connector) :
				   space(*this), socket(-1),
				   m_Connector(connector), m_InBuf(), m_OutBuf(),
				   m_Encoder(m_OutBuf), m_Decoder(m_InBuf),
				   m_EndDecoded(m_InBuf.begin()),
				   m_EndEncoded(m_OutBuf.begin())
{
	LOG_DEBUG("Creating connection...");
	memset(&status, 0, sizeof(status));
	rlist_create(&m_in_write);
	rlist_create(&m_in_read);
}

template<class BUFFER, class NetProvider>
Connection<BUFFER, NetProvider>::~Connection()
{
	if (socket >= 0) {
		m_Connector.close(*this);
		socket = -1;
	}
	if (! rlist_empty(&m_in_write)) {
		rlist_del(&m_in_write);
		LOG_WARNING("Connection ", this, " had unsent data in output buffer!");
	}
	if (! rlist_empty(&m_in_read)) {
		rlist_del(&m_in_read);
		LOG_WARNING("Connection ", this, " had unread data in input buffer!");
	}
}

template<class BUFFER, class NetProvider>
std::optional<Response<BUFFER>>
Connection<BUFFER, NetProvider>::getResponse(rid_t future)
{
	auto entry = m_Futures.find(future);
	if (entry == m_Futures.end())
		return std::nullopt;
	Response<BUFFER> response = std::move(entry->second);
	m_Futures.erase(future);
	return std::make_optional(std::move(response));
}

template<class BUFFER, class NetProvider>
bool
Connection<BUFFER, NetProvider>::futureIsReady(rid_t future)
{
	return m_Futures.find(future) != m_Futures.end();
}

template<class BUFFER, class NetProvider>
void
Connection<BUFFER, NetProvider>::readyToDecode()
{
	m_Connector.readyToDecode(*this);
}

template<class BUFFER, class NetProvider>
template <class T>
rid_t
Connection<BUFFER, NetProvider>::call(const std::string &func, const T &args)
{
	m_EndEncoded += m_Encoder.encodeCall(func, args);
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}

template<class BUFFER, class NetProvider>
rid_t
Connection<BUFFER, NetProvider>::ping()
{
	m_EndEncoded += m_Encoder.encodePing();
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}

template<class BUFFER, class NetProvider>
template <class T>
rid_t
Connection<BUFFER, NetProvider>::insert(const T &tuple, uint32_t space_id)
{
	m_EndEncoded += m_Encoder.encodeInsert(tuple, space_id);
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}

template<class BUFFER, class NetProvider>
template <class T>
rid_t
Connection<BUFFER, NetProvider>::replace(const T &tuple, uint32_t space_id)
{
	m_EndEncoded += m_Encoder.encodeReplace(tuple, space_id);
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}

template<class BUFFER, class NetProvider>
template <class T>
rid_t
Connection<BUFFER, NetProvider>::delete_(const T &key, uint32_t space_id,
					 uint32_t index_id)
{
	m_EndEncoded += m_Encoder.encodeDelete(key, space_id, index_id);
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}

template<class BUFFER, class NetProvider>
template <class K, class T>
rid_t
Connection<BUFFER, NetProvider>::update(const K &key, const T &tuple,
					uint32_t space_id, uint32_t index_id)
{
	m_EndEncoded += m_Encoder.encodeUpdate(key, tuple, space_id, index_id);
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}

template<class BUFFER, class NetProvider>
template <class T, class O>
rid_t
Connection<BUFFER, NetProvider>::upsert(const T &tuple, const O &ops,
					uint32_t space_id, uint32_t index_base)
{
	m_EndEncoded += m_Encoder.encodeUpsert(tuple, ops, space_id, index_base);
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}

template<class BUFFER, class NetProvider>
template <class T>
rid_t
Connection<BUFFER, NetProvider>::select(const T &key, uint32_t space_id,
					uint32_t index_id, uint32_t limit,
					uint32_t offset, IteratorType iterator)
{
	m_EndEncoded += m_Encoder.encodeSelect(key, space_id, index_id, limit,
					       offset, iterator);
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}

template<class BUFFER, class NetProvider>
template <class T>
rid_t
Connection<BUFFER, NetProvider>::execute(const std::string& statement, const T& parameters)
{
	m_EndEncoded += m_Encoder.encodeExecute(statement, parameters);
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}

template<class BUFFER, class NetProvider>
<<<<<<< HEAD
template <class T>
rid_t
Connection<BUFFER, NetProvider>::execute(unsigned int stmt_id, const T& parameters)
{
	m_EndEncoded += m_Encoder.encodeExecute(stmt_id, parameters);
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}

template<class BUFFER, class NetProvider>
=======
>>>>>>> fc37e11... Client: prepare()
rid_t
Connection<BUFFER, NetProvider>::prepare(const std::string& statement)
{
	m_EndEncoded += m_Encoder.encodePrepare(statement);
	m_Connector.readyToSend(*this);
	return RequestEncoder<BUFFER>::getSync();
}



template<class BUFFER, class NetProvider>
void
Connection<BUFFER, NetProvider>::setError(const std::string &msg)
{
	m_Error.msg = msg;
	status.is_failed = true;
}

template<class BUFFER, class NetProvider>
std::string&
Connection<BUFFER, NetProvider>::getError()
{
	return m_Error.msg;
}

template<class BUFFER, class NetProvider>
void
Connection<BUFFER, NetProvider>::reset()
{
	std::memset(&status, 0, sizeof(status));
}

template<class BUFFER, class NetProvider>
BUFFER&
Connection<BUFFER, NetProvider>::getInBuf()
{
	return m_InBuf;
}


#ifndef NDEBUG
template<class BUFFER, class NetProvider>
std::string
Connection<BUFFER, NetProvider>::toString()
{
	return "Socket " + std::to_string(socket) + ", OutBuf: " +
		std::to_string(m_EndEncoded - m_OutBuf.begin()) + " bytes to send;" +
		"InBuf: " + std::to_string(m_EndDecoded - m_InBuf.begin()) + " bytes to decode";
}
#endif

template<class BUFFER, class NetProvider>
struct iovec *
inBufferToIOV(Connection<BUFFER, NetProvider> &conn, size_t size, size_t *iov_len)
{
	assert(iov_len != NULL);
	BUFFER &buf = conn.m_InBuf;
	struct iovec *vecs = conn.m_IOVecs;
	typename BUFFER::iterator itr = buf.end();
	buf.addBack(wrap::Advance{size});
	*iov_len = buf.getIOV(itr, vecs,
			      Connection<BUFFER, NetProvider>::AVAILABLE_IOVEC_COUNT);
	return vecs;
}

template<class BUFFER, class NetProvider>
struct iovec *
outBufferToIOV(Connection<BUFFER, NetProvider> &conn, size_t *iov_len)
{
	assert(iov_len != NULL);
	BUFFER &buf = conn.m_OutBuf;
	struct iovec *vecs = conn.m_IOVecs;
	*iov_len = buf.getIOV(buf.begin(), conn.m_EndEncoded, vecs,
			      Connection<BUFFER, NetProvider>::AVAILABLE_IOVEC_COUNT);
	return vecs;
}

template<class BUFFER, class NetProvider>
void
hasSentBytes(Connection<BUFFER, NetProvider> &conn, size_t bytes)
{
	if (bytes > 0)
		conn.m_OutBuf.dropFront(bytes);
	if (! hasDataToSend(conn)) {
		conn.status.is_ready_to_send = false;
		rlist_del(&conn.m_in_write);
		LOG_DEBUG("Removed ", &conn.m_in_write, " from the write list");
	}
}

template<class BUFFER, class NetProvider>
void
hasNotRecvBytes(Connection<BUFFER, NetProvider> &conn, size_t bytes)
{
	if (bytes > 0)
		conn.m_InBuf.dropBack(bytes);
}

template<class BUFFER, class NetProvider>
bool
hasDataToSend(Connection<BUFFER, NetProvider> &conn)
{
	return (conn.m_EndEncoded - conn.m_OutBuf.begin()) != 0;
}

template<class BUFFER, class NetProvider>
bool
hasDataToDecode(Connection<BUFFER, NetProvider> &conn)
{
	return conn.m_EndDecoded != conn.m_InBuf.end();
}

template<class BUFFER, class NetProvider>
DecodeStatus
decodeResponse(Connection<BUFFER, NetProvider> &conn)
{
	static int gc_step = 0;
	Response<BUFFER> response;
	response.size = conn.m_Decoder.decodeResponseSize();
	if (response.size < 0) {
		conn.setError("Failed to decode response size");
		return DECODE_ERR;
	}
	response.size += MP_RESPONSE_SIZE;
	if (! conn.m_InBuf.has(conn.m_EndDecoded, response.size)) {
		conn.m_Decoder.reset(conn.m_EndDecoded);
		return DECODE_NEEDMORE;
	}
	if (conn.m_Decoder.decodeResponse(response) != 0) {
		conn.setError("Failed to decode response, skipping bytes..");
		conn.m_EndDecoded += response.size;
		return DECODE_ERR;
	}
	LOG_DEBUG("Header: sync=", response.header.sync, ", code=",
		  response.header.code, ", schema=", response.header.schema_id);
	std::size_t response_size = response.size;
	conn.m_Futures.insert({response.header.sync, std::move(response)});
	conn.m_EndDecoded += response_size;
	if ((gc_step++ % Connection<BUFFER, NetProvider>::GC_STEP_CNT) == 0)
		conn.m_InBuf.flush();
	if (! hasDataToDecode(conn)) {
		conn.status.is_ready_to_decode = false;
		rlist_del(&conn.m_in_read);
		LOG_DEBUG("Removed ", &conn.m_in_write, " from the read list");
	}
	return DECODE_SUCC;
}

template<class BUFFER, class NetProvider>
int
decodeGreeting(Connection<BUFFER, NetProvider> &conn)
{
	//TODO: that's not zero-copy, should be rewritten in that pattern.
	char greeting_buf[Iproto::GREETING_SIZE];
	conn.m_InBuf.get(conn.m_EndDecoded, greeting_buf, sizeof(greeting_buf));
	conn.m_EndDecoded += sizeof(greeting_buf);
	assert(conn.m_EndDecoded == conn.m_InBuf.end());
	conn.m_Decoder.reset(conn.m_EndDecoded);
	if (parseGreeting(std::string_view{greeting_buf, Iproto::GREETING_SIZE},
			  conn.m_Greeting) != 0)
		return -1;
	LOG_DEBUG("Version: ", conn.m_Greeting.version_id);

#ifndef NDEBUG
	//print salt in hex format.
	char hex_salt[Iproto::MAX_SALT_SIZE * 2 + 1];
	const char *hex = "0123456789abcdef";
	for (size_t i = 0; i < conn.m_Greeting.salt_size; i++) {
		uint8_t u = conn.m_Greeting.salt[i];
		hex_salt[i * 2] = hex[u / 16];
		hex_salt[i * 2 + 1] = hex[u % 16];
	}
	hex_salt[conn.m_Greeting.salt_size * 2] = 0;
	LOG_DEBUG("Salt: ", hex_salt);
#endif
	return 0;
}
