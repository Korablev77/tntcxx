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
#include "Utils/Helpers.hpp"
#include "Utils/TupleReader.hpp"
#include "Utils/System.hpp"

#include "../src/Client/LibevNetProvider.hpp"
#include "../src/Client/Connector.hpp"

const char *localhost = "127.0.0.1";
int port = 3301;
int WAIT_TIMEOUT = 1000; //milliseconds

using Net_t = DefaultNetProvider<Buf_t, NetworkEngine>;


enum ResultFormat {
	TUPLES = 0,
	MULTI_RETURN,
	SELECT_RETURN
};

template <class BUFFER, class NetProvider = Net_t>
void
printResponse(Connection<BUFFER, NetProvider> &conn, Response<BUFFER> &response,
	       enum ResultFormat format = TUPLES)
{
	if (response.body.error_stack != std::nullopt) {
		Error err = (*response.body.error_stack).error;
		std::cout << "RESPONSE ERROR: msg=" << err.msg <<
			  " line=" << err.file << " file=" << err.file <<
			  " errno=" << err.saved_errno <<
			  " type=" << err.type_name <<
			  " code=" << err.errcode << std::endl;
		return;
	}
	if (response.body.sql_info  != std::nullopt) {
		std::cout << "Row count = " << response.body.sql_info->row_count << std::endl;
	}
	if (response.body.metadata != std::nullopt) {
		std::vector<ColumnMap>& maps = response.body.metadata->column_maps;
		std::cout << "Metadata:\n";
		for (auto& map : maps) {
			std::cout << "Field name: " << map.field_name << std::endl;
			std::cout << "Field name len: " << map.field_name_len << std::endl;
			std::cout << "Field type: " << map.field_type << std::endl;
			std::cout << "Field type len: " << map.field_type_len << std::endl;
			std::cout << "Collation: " << map.collation << std::endl;
			std::cout << "Collation len: " << map.collation_len << std::endl;
			std::cout << "Is nullable: " << map.is_nullable << std::endl;
			std::cout << "Is autoincrement: " << map.is_autoincrement << std::endl;
			std::cout << "Span: " << map.span << std::endl;
			std::cout << "Span len: " << map.span_len << std::endl;
		}
	}
	if (response.body.data != std::nullopt) {
		Data<BUFFER>& data = *response.body.data;
		if (data.tuples.empty()) {
			std::cout << "Empty result" << std::endl;
			return;
		}
		std::vector<UserTuple> tuples;
		switch (format) {
			case TUPLES:
				tuples = decodeUserTuple(conn.getInBuf(), data);
				break;
			case MULTI_RETURN:
				tuples = decodeMultiReturn(conn.getInBuf(), data);
				break;
			case SELECT_RETURN:
				tuples = decodeSelectReturn(conn.getInBuf(), data);
				break;
			default:
				assert(0);
		}
		for (auto const& t : tuples) {
			std::cout << t << std::endl;
		}
	}
}

template<class BUFFER, class NetProvider = Net_t>
bool
compareTupleResult(std::vector<UserTuple> &tuples,
		   std::vector<UserTuple> &expected);

template <class BUFFER, class NetProvider = Net_t>
void
trivial(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn(client);
	TEST_CASE("Nonexistent future");
	std::optional<Response<Buf_t>> response = conn.getResponse(666);
	fail_unless(response == std::nullopt);
	/* Execute request without connecting to the host. */
	TEST_CASE("No established connection");
	rid_t f = conn.ping();
	client.wait(conn, f, WAIT_TIMEOUT);
	fail_unless(conn.status.is_failed);
	/* Connect to the wrong address. */
	TEST_CASE("Bad address");
	int rc = client.connect(conn, "asdasd", port);
	fail_unless(rc != 0);
	TEST_CASE("Unreachable address");
	rc = client.connect(conn, "101.101.101", port);
	fail_unless(rc != 0);
	TEST_CASE("Wrong port");
	rc = client.connect(conn, localhost, -666);
	fail_unless(rc != 0);
	TEST_CASE("Connect timeout");
	rc = client.connect(conn, "8.8.8.8", port, 0);
	fail_unless(rc != 0);
}

/** Single connection, separate/sequence pings, no errors */
template <class BUFFER, class NetProvider = Net_t>
void
single_conn_ping(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn(client);
	int rc = client.connect(conn, localhost, port);
	fail_unless(rc == 0);
	rid_t f = conn.ping();
	fail_unless(!conn.futureIsReady(f));
	client.wait(conn, f, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f));
	std::optional<Response<Buf_t>> response = conn.getResponse(f);
	fail_unless(response != std::nullopt);
	fail_unless(response->header.code == 0);
	f = conn.ping();
	client.wait(conn, f, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f));
	/* Second wait() should terminate immediately. */
	client.wait(conn, f, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f));
	response = conn.getResponse(f);
	fail_unless(response != std::nullopt);
	fail_unless(response->header.code == 0);
	/* Many requests at once. */
	rid_t features[3];
	features[0] = conn.ping();
	features[1] = conn.ping();
	features[2] = conn.ping();
	client.waitAll(conn, (rid_t *) &features, 3, 1);
	for (int i = 0; i < 3; ++i) {
		fail_unless(conn.futureIsReady(features[i]));
		response = conn.getResponse(features[i]);
		fail_unless(response != std::nullopt);
		fail_unless(response->header.code == 0);
		fail_unless(response->body.error_stack == std::nullopt);
	}
	client.close(conn);
}

/** Several connection, separate/sequence pings, no errors */
template <class BUFFER, class NetProvider = Net_t>
void
many_conn_ping(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn1(client);
	Connection<Buf_t, NetProvider> conn2(client);
	Connection<Buf_t, NetProvider> conn3(client);
	int rc = client.connect(conn1, localhost, port);
	fail_unless(rc == 0);
	/* Try to connect to the same port */
	rc = client.connect(conn2, localhost, port);
	fail_unless(rc == 0);
	/*
	 * Try to re-connect to another address whithout closing
	 * current connection.
	 */
	rc = client.connect(conn2, localhost, port + 2);
	fail_unless(rc != 0);
	rc = client.connect(conn3, localhost, port);
	fail_unless(rc == 0);
	rid_t f1 = conn1.ping();
	rid_t f2 = conn2.ping();
	rid_t f3 = conn3.ping();
	Connection<Buf_t, NetProvider> *conn = client.waitAny(WAIT_TIMEOUT);
	(void) conn;
	fail_unless(conn1.futureIsReady(f1) || conn2.futureIsReady(f2) ||
		    conn3.futureIsReady(f3));
	client.close(conn1);
	client.close(conn2);
	client.close(conn3);
}

/** Single connection, errors in response. */
template <class BUFFER, class NetProvider = Net_t>
void
single_conn_error(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn(client);
	int rc = client.connect(conn, localhost, port);
	fail_unless(rc == 0);
	/* Fake space id. */
	uint32_t space_id = -111;
	std::tuple data = std::make_tuple(666);
	rid_t f1 = conn.space[space_id].replace(data);
	client.wait(conn, f1, WAIT_TIMEOUT);
	std::optional<Response<Buf_t>> response = conn.getResponse(f1);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.error_stack != std::nullopt);
	/* Wrong tuple format: missing fields. */
	space_id = 512;
	data = std::make_tuple(666);
	f1 = conn.space[space_id].replace(data);
	client.wait(conn, f1, WAIT_TIMEOUT);
	response = conn.getResponse(f1);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.error_stack != std::nullopt);
	/* Wrong tuple format: type mismatch. */
	space_id = 512;
	std::tuple another_data = std::make_tuple(666, "asd", "asd");
	f1 = conn.space[space_id].replace(another_data);
	client.wait(conn, f1, WAIT_TIMEOUT);
	response = conn.getResponse(f1);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.error_stack != std::nullopt);

	client.close(conn);
}

/** Single connection, separate replaces */
template <class BUFFER, class NetProvider = Net_t>
void
single_conn_replace(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn(client);
	int rc = client.connect(conn, localhost, port);
	fail_unless(rc == 0);
	uint32_t space_id = 512;
	std::tuple data = std::make_tuple(666, "111", 1.01);
	rid_t f1 = conn.space[space_id].replace(data);
	data = std::make_tuple(777, "asd", 2.02);
	rid_t f2 = conn.space[space_id].replace(data);

	client.wait(conn, f1, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f1));
	std::optional<Response<Buf_t>> response = conn.getResponse(f1);
	printResponse<BUFFER, NetProvider>(conn, *response);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);

	client.wait(conn, f2, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f2));
	response = conn.getResponse(f2);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);

	client.close(conn);
}

/** Single connection, separate inserts */
template <class BUFFER, class NetProvider = Net_t>
void
single_conn_insert(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn(client);
	int rc = client.connect(conn, localhost, port);
	fail_unless(rc == 0);
	TEST_CASE("Successful inserts");
	uint32_t space_id = 512;
	std::tuple data = std::make_tuple(123, "insert", 3.033);
	rid_t f1 = conn.space[space_id].insert(data);
	data = std::make_tuple(321, "another_insert", 2.022);
	rid_t f2 = conn.space[space_id].insert(data);

	client.wait(conn, f1, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f1));
	std::optional<Response<Buf_t>> response = conn.getResponse(f1);
	printResponse<BUFFER, NetProvider>(conn, *response);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);

	client.wait(conn, f2, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f2));
	response = conn.getResponse(f2);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);

	TEST_CASE("Duplicate key during insertion");
	data = std::make_tuple(321, "another_insert", 2.022);
	rid_t f3 = conn.space[space_id].insert(data);
	client.wait(conn, f3, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f3));
	response = conn.getResponse(f3);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.error_stack != std::nullopt);
	printResponse<BUFFER, NetProvider>(conn, *response);

	client.close(conn);
}

/** Single connection, separate updates */
template <class BUFFER, class NetProvider = Net_t>
void
single_conn_update(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn(client);
	int rc = client.connect(conn, localhost, port);
	fail_unless(rc == 0);
	TEST_CASE("Successful update");
	uint32_t space_id = 512;
	std::tuple key = std::make_tuple(123);
	std::tuple op1 = std::make_tuple("=", 1, "update");
	std::tuple op2 = std::make_tuple("+", 2, 12);
	rid_t f1 = conn.space[space_id].update(key, std::make_tuple(op1, op2));
	key = std::make_tuple(321);
	std::tuple op3 = std::make_tuple(":", 1, 2, 1, "!!");
	std::tuple op4 = std::make_tuple("-", 2, 5.05);
	rid_t f2 = conn.space[space_id].update(key, std::make_tuple(op3, op4));

	client.wait(conn, f1, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f1));
	std::optional<Response<Buf_t>> response = conn.getResponse(f1);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);

	client.wait(conn, f2, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f2));
	response = conn.getResponse(f2);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);

	client.close(conn);
}

/** Single connection, separate deletes */
template <class BUFFER, class NetProvider = Net_t>
void
single_conn_delete(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn(client);
	int rc = client.connect(conn, localhost, port);
	fail_unless(rc == 0);
	TEST_CASE("Successful deletes");
	uint32_t space_id = 512;
	std::tuple key = std::make_tuple(123);
	rid_t f1 = conn.space[space_id].delete_(key);
	key = std::make_tuple(321);
	rid_t f2 = conn.space[space_id].index[0].delete_(key);

	client.wait(conn, f1, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f1));
	std::optional<Response<Buf_t>> response = conn.getResponse(f1);
	printResponse<BUFFER, NetProvider>(conn, *response);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);

	client.wait(conn, f2, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f2));
	response = conn.getResponse(f2);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);

	TEST_CASE("Delete by wrong key (empty response)");
	key = std::make_tuple(10101);
	rid_t f3 = conn.space[space_id].delete_(key);
	client.wait(conn, f3, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f3));
	response = conn.getResponse(f3);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	printResponse<BUFFER, NetProvider>(conn, *response);

	client.close(conn);
}

/** Single connection, separate upserts */
template <class BUFFER, class NetProvider = Net_t>
void
single_conn_upsert(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn(client);
	int rc = client.connect(conn, localhost, port);
	fail_unless(rc == 0);
	TEST_CASE("upsert-insert");
	uint32_t space_id = 512;
	std::tuple tuple = std::make_tuple(333, "upsert-insert", 0.0);
	std::tuple op1 = std::make_tuple("=", 1, "upsert");
	rid_t f1 = conn.space[space_id].upsert(tuple, std::make_tuple(op1));

	client.wait(conn, f1, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f1));
	std::optional<Response<Buf_t>> response = conn.getResponse(f1);
	fail_unless(response->body.data != std::nullopt);

	TEST_CASE("upsert-update");
	tuple = std::make_tuple(666, "111", 1.01);
	std::tuple op2 =  std::make_tuple("=", 1, "upsert-update");
	rid_t f2 = conn.space[space_id].upsert(tuple, std::make_tuple(op2));
	client.wait(conn, f2, WAIT_TIMEOUT);
	response = conn.getResponse(f2);
	fail_unless(response->body.data != std::nullopt);

	client.close(conn);
}

/** Single connection, select single tuple */
template <class BUFFER, class NetProvider = Net_t>
void
single_conn_select(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn(client);
	int rc = client.connect(conn, localhost, port);
	fail_unless(rc == 0);
	uint32_t space_id = 512;
	uint32_t index_id = 0;
	uint32_t limit = 1;
	uint32_t offset = 0;
	IteratorType iter = IteratorType::EQ;

	auto s = conn.space[space_id];
	rid_t f1 = s.select(std::make_tuple(666));
	rid_t f2 = s.index[index_id].select(std::make_tuple(777));
	rid_t f3 = s.select(std::make_tuple(-1), index_id, limit, offset, iter);
	rid_t f4 = s.select(std::make_tuple(), index_id, limit + 3, offset, IteratorType::ALL);

	client.wait(conn, f1, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f1));
	std::optional<Response<Buf_t>> response = conn.getResponse(f1);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);
	printResponse<BUFFER, NetProvider>(conn, *response);

	client.wait(conn, f2, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f2));
	response = conn.getResponse(f2);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);
	printResponse<BUFFER, NetProvider>(conn, *response);

	client.wait(conn, f3, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f3));
	response = conn.getResponse(f3);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);
	printResponse<BUFFER, NetProvider>(conn, *response);

	client.wait(conn, f4, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f4));
	response = conn.getResponse(f4);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);
	printResponse<BUFFER, NetProvider>(conn, *response);

	client.close(conn);
}

/** Single connection, call procedure with arguments */
template <class BUFFER, class NetProvider = Net_t>
void
single_conn_call(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	const static char *return_replace = "remote_replace";
	const static char *return_select  = "remote_select";
	const static char *return_uint    = "remote_uint";
	const static char *return_multi   = "remote_multi";

	Connection<Buf_t, NetProvider> conn(client);
	int rc = client.connect(conn, localhost, port);
	fail_unless(rc == 0);

	TEST_CASE("call remote_replace");
	rid_t f1 = conn.call(return_replace, std::make_tuple(5, "value_from_test", 5.55));
	rid_t f2 = conn.call(return_replace, std::make_tuple(6, "value_from_test2", 3.33));

	client.wait(conn, f1, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f1));
	std::optional<Response<Buf_t>> response = conn.getResponse(f1);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);
	printResponse<BUFFER, NetProvider>(conn, *response);

	client.wait(conn, f2, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f2));
	response = conn.getResponse(f2);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.error_stack == std::nullopt);
	printResponse<BUFFER, NetProvider>(conn, *response);

	TEST_CASE("call remote_uint");
	rid_t f4 = conn.call(return_uint, std::make_tuple());
	client.wait(conn, f4, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f4));
	response = conn.getResponse(f4);
	printResponse<BUFFER, NetProvider>(conn, *response, MULTI_RETURN);

	TEST_CASE("call remote_multi");
	rid_t f5 = conn.call(return_multi, std::make_tuple());
	client.wait(conn, f5, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f5));
	response = conn.getResponse(f5);
	printResponse<BUFFER, NetProvider>(conn, *response, MULTI_RETURN);

	TEST_CASE("call remote_select");
	rid_t f6 = conn.call(return_select, std::make_tuple());
	client.wait(conn, f6, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f6));
	response = conn.getResponse(f6);
	printResponse<BUFFER, NetProvider>(conn, *response, SELECT_RETURN);

	/*
	 * Also test that errors during call are handled properly:
	 * call non-existent function and pass wrong number of arguments.
	 */
	TEST_CASE("call wrong function");
	rid_t f7 = conn.call("wrong_name", std::make_tuple(7, "aaa", 0.0));
	client.wait(conn, f7, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f7));
	response = conn.getResponse(f7);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.error_stack != std::nullopt);
	printResponse<BUFFER, NetProvider>(conn, *response);

	TEST_CASE("call function with wrong number of arguments");
	rid_t f8 = conn.call(return_replace, std::make_tuple(7));
	client.wait(conn, f8, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(f8));
	response = conn.getResponse(f8);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.error_stack != std::nullopt);
	printResponse<BUFFER, NetProvider>(conn, *response);

	client.close(conn);
}


/** Single connection, separate executes */
template <class BUFFER, class NetProvider = Net_t>
void
single_conn_execute(Connector<BUFFER, NetProvider> &client)
{
	TEST_INIT(0);
	Connection<Buf_t, NetProvider> conn(client);
	int rc = client.connect(conn, localhost, port);
	fail_unless(rc == 0);

	TEST_CASE("CREATE TABLE");
	rid_t create_table = conn.execute("CREATE TABLE IF NOT EXISTS testing_sql "
									  "(column1 UNSIGNED PRIMARY KEY, "
									  "column2 VARCHAR(50));", std::make_tuple());
	
	client.wait(conn, create_table, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(create_table));
	std::optional<Response<Buf_t>> response = conn.getResponse(create_table);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.sql_info != std::nullopt);

	TEST_CASE("Simple INSERT");
	rid_t insert = conn.execute("INSERT INTO testing_sql VALUES (20, 'first');", std::make_tuple());
	client.wait(conn, insert, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(insert));
	response = conn.getResponse(insert);
	fail_unless(response != std::nullopt);
	
	if (response->body.error_stack != std::nullopt) {
		std::cout << response->body.error_stack->error.msg << std::endl;
	}
	
	fail_unless(response->body.sql_info != std::nullopt);
	fail_unless(response->body.sql_info->row_count == 1);

	TEST_CASE("INSERT with binding arguments");
	std::tuple args = std::make_tuple(response->body.sql_info->row_count, "Timur", 2, "Nikita", 3, "Anastas");
	rid_t insert_args = conn.execute("INSERT INTO testing_sql VALUES (?, ?), (?, ?), (?, ?);", args);
	client.wait(conn, insert_args, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(insert_args));
	response = conn.getResponse(insert_args);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.sql_info != std::nullopt);
	fail_unless(response->body.sql_info->row_count == 3);


	TEST_CASE("SELECT");
	rid_t select = conn.execute("SELECT * FROM testing_sql;", std::make_tuple());
	client.wait(conn, select, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(select));
	response = conn.getResponse(select);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.metadata != std::nullopt);
	
	printResponse<BUFFER, NetProvider>(conn, *response);


	TEST_CASE("prepare");
	rid_t pr_select = conn.prepare("SELECT * FROM testing_sql;");
	client.wait(conn, pr_select, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(pr_select));
	response = conn.getResponse(pr_select);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.stmt_id != std::nullopt);
	fail_unless(response->body.bind_count != std::nullopt);
	
	if (response->body.error_stack != std::nullopt) {
		std::cout << response->body.error_stack->error.msg << std::endl;
	}

	printResponse<BUFFER, NetProvider>(conn, *response);

	TEST_CASE("execute SELECT by stmt_id");
	rid_t exec_by_id = conn.execute(*response->body.stmt_id, std::make_tuple());
	client.wait(conn, exec_by_id, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(exec_by_id));
	response = conn.getResponse(exec_by_id);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.data != std::nullopt);
	fail_unless(response->body.metadata != std::nullopt);
	
	printResponse<BUFFER, NetProvider>(conn, *response);

	TEST_CASE("prepare with binding");
	rid_t pr_insert = conn.prepare("INSERT INTO testing_sql VALUES (?, ?, ?);");
	client.wait(conn, pr_insert, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(pr_insert));
	response = conn.getResponse(pr_insert);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.stmt_id != std::nullopt);
	fail_unless(response->body.bind_count != std::nullopt);


	TEST_CASE("execute INSERT by stmt_id with binding");
	rid_t exec_by_id_ins = conn.execute(*response->body.stmt_id, std::make_tuple(5, "Stranger", 7.9));
	client.wait(conn, exec_by_id_ins, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(exec_by_id_ins));
	response = conn.getResponse(exec_by_id_ins);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.sql_info != std::nullopt);
	fail_unless(response->body.sql_info->row_count == 1);


	TEST_CASE("DROP TABLE");
	rid_t drop_table = conn.execute("DROP TABLE IF EXISTS testing_sql;", std::make_tuple());
	client.wait(conn, drop_table, WAIT_TIMEOUT);
	fail_unless(conn.futureIsReady(drop_table));
	response = conn.getResponse(drop_table);
	fail_unless(response != std::nullopt);
	fail_unless(response->body.sql_info != std::nullopt);

	client.close(conn);
}






int main()
{
	if (cleanDir() != 0)
		return -1;
	if (launchTarantool() != 0)
		return -1;
	sleep(1);
	Connector<Buf_t> client;
	trivial(client);
	single_conn_ping<Buf_t>(client);
	many_conn_ping<Buf_t>(client);
	single_conn_error<Buf_t>(client);
	single_conn_replace<Buf_t>(client);
	single_conn_insert<Buf_t>(client);
	single_conn_update<Buf_t>(client);
	single_conn_delete<Buf_t>(client);
	single_conn_upsert<Buf_t>(client);
	single_conn_select<Buf_t>(client);
	single_conn_call<Buf_t>(client);
	single_conn_execute<Buf_t>(client);

	/* LibEv network provide */
	using NetLibEv_t = LibevNetProvider<Buf_t, NetworkEngine>;
	Connector<Buf_t, NetLibEv_t > another_client;
	trivial<Buf_t, NetLibEv_t >(another_client);
	single_conn_ping<Buf_t, NetLibEv_t>(another_client);
	many_conn_ping<Buf_t, NetLibEv_t>(another_client);
	single_conn_error<Buf_t, NetLibEv_t>(another_client);
	single_conn_replace<Buf_t, NetLibEv_t>(another_client);
	single_conn_insert<Buf_t, NetLibEv_t>(another_client);
	single_conn_update<Buf_t, NetLibEv_t>(another_client);
	single_conn_delete<Buf_t, NetLibEv_t>(another_client);
	single_conn_upsert<Buf_t, NetLibEv_t>(another_client);
	single_conn_select<Buf_t, NetLibEv_t>(another_client);
	single_conn_call<Buf_t, NetLibEv_t>(another_client);
	return 0;
}
