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

namespace Iproto {
	enum {
		BODY_LEN_MAX = 2147483648UL,
		XLOG_FIXHEADER_SIZE = 19,
		DIAG_ERRMSG_MAX = 512,
		GREETING_SIZE = 128,
		GREETING_LINE1_SIZE = 64,
		GREETING_LINE2_SIZE = 64,
		GREETING_MAX_SALT_SIZE = 44,
		MAX_SALT_SIZE = 32,
		SCRAMBLE_SIZE = 20,
		DIAG_FILENAME_MAX = 256,
		DIAG_TYPENAME_MAX = 24,
	};

	enum {
		FLAG_COMMIT = 0x01,
	};


	enum Key {
		REQUEST_TYPE = 0x00,
		SYNC = 0x01,
		REPLICA_ID = 0x02,
		LSN = 0x03,
		TIMESTAMP = 0x04,
		SCHEMA_VERSION = 0x05,
		SERVER_VERSION = 0x06,
		GROUP_ID = 0x07,
		TSN = 0x08,
		FLAGS = 0x09,
		SPACE_ID = 0x10,
		INDEX_ID = 0x11,
		LIMIT = 0x12,
		OFFSET = 0x13,
		ITERATOR = 0x14,
		INDEX_BASE = 0x15,
		KEY = 0x20,
		TUPLE = 0x21,
		FUNCTION_NAME = 0x22,
		USER_NAME = 0x23,
		INSTANCE_UUID = 0x24,
		CLUSTER_UUID = 0x25,
		VCLOCK = 0x26,
		EXPR = 0x27,
		OPS = 0x28,
		BALLOT = 0x29,
		TUPLE_META = 0x2a,
		OPTIONS = 0x2b,
		DATA = 0x30,
		ERROR_24 = 0x31,
		METADATA = 0x32,
		BIND_METADATA = 0x33,
		BIND_COUNT = 0x34,
		SQL_TEXT = 0x40,
		SQL_BIND = 0x41,
		SQL_INFO = 0x42,
		STMT_ID = 0x43,
		REPLICA_ANON = 0x50,
		ID_FILTER = 0x51,
		ERROR = 0x52,
		KEY_MAX
	};

	enum MetadataKey {
		FIELD_NAME = 0,
		FIELD_TYPE = 1,
		FIELD_COLL = 2,
		FIELD_IS_NULLABLE = 3,
		FIELD_IS_AUTOINCREMENT = 4,
		FIELD_SPAN = 5,
	};

	enum ColumnMap {
		FIELD_NAME_MAX = 256,
		FIELD_TYPE_NAME_MAX = 32,
		COLLATION_MAX = 32,
		SPAN_MAX = 256
	};

	enum Type {
		OK = 0,
		SELECT = 1,
		INSERT = 2,
		REPLACE = 3,
		UPDATE = 4,
		DELETE = 5,
		CALL_16 = 6,
		AUTH = 7,
		EVAL = 8,
		UPSERT = 9,
		CALL = 10,
		EXECUTE = 11,
		NOP = 12,
		PREPARE = 13,
		TYPE_STAT_MAX,
		RAFT = 30,
		CONFIRM = 40,
		ROLLBACK = 41,
		PING = 64,
		JOIN = 65,
		SUBSCRIBE = 66,
		VOTE_DEPRECATED = 67,
		VOTE = 68,
		FETCH_SNAPSHOT = 69,
		REGISTER = 70,
		VY_INDEX_RUN_INFO = 100,
		VY_INDEX_PAGE_INFO = 101,
		VY_RUN_ROW_INDEX = 102,
		CHUNK = 128,
		TYPE_ERROR = 1 << 15
	};

	/** Keys of IPROTO_SQL_INFO map. */
	enum SqlInfoKey {
		SQL_INFO_ROW_COUNT = 0x00,
		SQL_INFO_AUTOINCREMENT_IDS = 0x01
	};

	enum ErrorStack {
		ERROR_STACK = 0x00
	};

	enum Error {
		ERROR_TYPE = 0x00,
		ERROR_FILE = 0x01,
		ERROR_LINE = 0x02,
		ERROR_MESSAGE = 0x03,
		ERROR_ERRNO = 0x04,
		ERROR_CODE = 0x05,
		ERROR_FIELDS = 0x06,
		ERROR_MAX,
	};
}
