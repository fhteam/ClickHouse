#include <sql.h>
#include <sqlext.h>

#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include <iostream>
#include <sstream>
#include <stdexcept>

#include <Poco/NumberFormatter.h>

#include "StringRef.h"
#include "Log.h"
#include "DiagnosticRecord.h"
#include "Environment.h"
#include "Connection.h"
#include "Statement.h"
#include "ResultSet.h"
#include "utils.h"


/** Проверяет handle. Ловит исключения и засовывает их в DiagnosticRecord.
  */
template <typename Handle, typename F>
RETCODE doWith(SQLHANDLE handle_opaque, F && f)
{
	if (nullptr == handle_opaque)
		return SQL_INVALID_HANDLE;

	Handle & handle = *reinterpret_cast<Handle *>(handle_opaque);

	try
	{
		handle.diagnostic_record.reset();
		return f(handle);
	}
	catch (...)
	{
		handle.diagnostic_record.fromException();
		return SQL_ERROR;
	}
}


RETCODE allocEnv(SQLHENV * out_environment)
{
	if (nullptr == out_environment)
		return SQL_INVALID_HANDLE;

	*out_environment = new Environment;
	return SQL_SUCCESS;
}

RETCODE allocConnect(SQLHENV environment, SQLHDBC * out_connection)
{
	if (nullptr == out_connection)
		return SQL_INVALID_HANDLE;

	*out_connection = new Connection(*reinterpret_cast<Environment *>(environment));
	return SQL_SUCCESS;
}

RETCODE allocStmt(SQLHDBC connection, SQLHSTMT * out_statement)
{
	if (nullptr == out_statement || nullptr == connection)
		return SQL_INVALID_HANDLE;

	*out_statement = new Statement(*reinterpret_cast<Connection *>(connection));
	return SQL_SUCCESS;
}

RETCODE freeEnv(SQLHENV environment)
{
	delete reinterpret_cast<Environment *>(environment);
	return SQL_SUCCESS;
}

RETCODE freeConnect(SQLHDBC connection)
{
	delete reinterpret_cast<Connection *>(connection);
	return SQL_SUCCESS;
}

RETCODE freeStmt(SQLHSTMT statement)
{
	delete reinterpret_cast<Statement *>(statement);
	return SQL_SUCCESS;
}


/// Интерфейс библиотеки.
extern "C"
{


RETCODE SQL_API
SQLAllocHandle(SQLSMALLINT handle_type,
               SQLHANDLE input_handle,
               SQLHANDLE * output_handle)
{
	LOG(__FUNCTION__);

	switch (handle_type)
	{
		case SQL_HANDLE_ENV:
			return allocEnv((SQLHENV *)output_handle);
		case SQL_HANDLE_DBC:
			return allocConnect((SQLHENV)input_handle, (SQLHDBC *)output_handle);
		case SQL_HANDLE_STMT:
			return allocStmt((SQLHDBC)input_handle, (SQLHSTMT *)output_handle);
		default:
			return SQL_ERROR;
	}
}

RETCODE SQL_API
SQLAllocEnv(SQLHDBC * output_handle)
{
	LOG(__FUNCTION__);
	return allocEnv(output_handle);
}

RETCODE SQL_API
SQLAllocConnect(SQLHENV input_handle, SQLHDBC * output_handle)
{
	LOG(__FUNCTION__);
	return allocConnect(input_handle, output_handle);
}

RETCODE SQL_API
SQLAllocStmt(SQLHDBC input_handle, SQLHSTMT * output_handle)
{
	LOG(__FUNCTION__);
	return allocStmt(input_handle, output_handle);
}


RETCODE SQL_API
SQLFreeHandle(SQLSMALLINT handleType, SQLHANDLE handle)
{
	LOG(__FUNCTION__);

	switch (handleType)
	{
		case SQL_HANDLE_ENV:
			return freeEnv((SQLHENV)handle);
		case SQL_HANDLE_DBC:
			return freeConnect((SQLHDBC)handle);
		case SQL_HANDLE_STMT:
			return freeStmt((SQLHDBC)handle);
		default:
			return SQL_ERROR;
	}
}


RETCODE SQL_API
SQLFreeEnv(HENV handle)
{
	LOG(__FUNCTION__);
	return freeEnv(handle);
}

RETCODE SQL_API
SQLFreeConnect(HDBC handle)
{
	LOG(__FUNCTION__);
	return freeConnect(handle);
}

RETCODE SQL_API
SQLFreeStmt(HSTMT statement_handle,
			SQLUSMALLINT option)
{
	LOG(__FUNCTION__);

	switch (option)
	{
		case SQL_DROP:
			return freeStmt(statement_handle);

		case SQL_CLOSE:				/// Закрыть курсор, проигнорировать оставшиеся результаты. Если курсора нет, то noop.
		case SQL_UNBIND:
		case SQL_RESET_PARAMS:
			return SQL_SUCCESS;

		default:
			return SQL_ERROR;
	}
}


RETCODE SQL_API
SQLConnect(HDBC connection_handle,
		   SQLCHAR * server_name, SQLSMALLINT server_name_size,
		   SQLCHAR * user, SQLSMALLINT user_size,
		   SQLCHAR * password, SQLSMALLINT password_size)
{
	LOG(__FUNCTION__);

	return doWith<Connection>(connection_handle, [&](Connection & connection)
	{
		std::string user_str = stringFromSQLChar(user, user_size);
		std::string password_str = stringFromSQLChar(password, password_size);

		connection.init("", 0, user_str, password_str, "");
		return SQL_SUCCESS;
	});
}


RETCODE SQL_API
SQLDriverConnect(HDBC connection_handle,
				 HWND unused_window,
				 SQLCHAR FAR * connection_str_in,
				 SQLSMALLINT connection_str_in_size,
				 SQLCHAR FAR * connection_str_out,
				 SQLSMALLINT connection_str_out_max_size,
				 SQLSMALLINT FAR * connection_str_out_size,
				 SQLUSMALLINT driver_completion)
{
	LOG(__FUNCTION__);

	return doWith<Connection>(connection_handle, [&](Connection & connection)
	{
		connection.init(stringFromSQLChar(connection_str_in, connection_str_in_size));
		return SQL_SUCCESS;
	});
}


RETCODE SQL_API
SQLGetInfo(HDBC connection_handle,
		   SQLUSMALLINT info_type,
		   PTR out_info_value, SQLSMALLINT out_info_value_max_length, SQLSMALLINT * out_info_value_length)
{
	LOG(__FUNCTION__);

	LOG("GetInfo with info_type: " << info_type << ", out_info_value_max_length: " << out_info_value_max_length);

	return doWith<Connection>(connection_handle, [&](Connection & connection)
	{
		const char * name = nullptr;

		switch (info_type)
		{
		#define CASE_FALLTHROUGH(NAME) \
			case NAME: \
				if (!name) name = #NAME;

		#define CASE_STRING(NAME, VALUE) \
			case NAME: \
				if (!name) name = #NAME; \
				LOG("GetInfo " << name << ", type: String, value: " << #VALUE << " = " << (VALUE)); \
				return fillOutputString(VALUE, out_info_value, out_info_value_max_length, out_info_value_length);

		#define CASE_NUM(NAME, TYPE, VALUE) \
			case NAME: \
				if (!name) name = #NAME; \
				LOG("GetInfo " << name << ", type: " << #TYPE << ", value: " << #VALUE << " = " << (VALUE)); \
				return fillOutputNumber<TYPE>(VALUE, out_info_value, out_info_value_max_length, out_info_value_length);

			CASE_STRING(SQL_DRIVER_VER, "1.0")
			CASE_STRING(SQL_DRIVER_ODBC_VER, "03.80")
			CASE_STRING(SQL_DM_VER, "03.80.0000.0000")
			CASE_STRING(SQL_DRIVER_NAME, "ClickHouse ODBC")
			CASE_STRING(SQL_DBMS_NAME, "ClickHouse")
			CASE_STRING(SQL_DBMS_VER, "01.00.0000")
			CASE_STRING(SQL_SERVER_NAME, connection.host)
			CASE_STRING(SQL_DATA_SOURCE_NAME, "ClickHouse")
			CASE_STRING(SQL_CATALOG_TERM, "catalog")
			CASE_STRING(SQL_COLLATION_SEQ, "UTF-8")
			CASE_STRING(SQL_DATABASE_NAME, connection.database)

			CASE_FALLTHROUGH(SQL_DATA_SOURCE_READ_ONLY)
			CASE_FALLTHROUGH(SQL_ACCESSIBLE_PROCEDURES)
			CASE_FALLTHROUGH(SQL_ACCESSIBLE_TABLES)
			CASE_FALLTHROUGH(SQL_CATALOG_NAME)
			CASE_FALLTHROUGH(SQL_EXPRESSIONS_IN_ORDERBY)
			CASE_STRING(SQL_COLUMN_ALIAS, "Y")

			CASE_FALLTHROUGH(SQL_ORDER_BY_COLUMNS_IN_SELECT)
			CASE_STRING(SQL_DESCRIBE_PARAMETER, "N")

			CASE_STRING(SQL_CATALOG_NAME_SEPARATOR, ".")
			CASE_STRING(SQL_IDENTIFIER_QUOTE_CHAR, "`")

			/// UINTEGER одиночные значения
			CASE_NUM(SQL_ODBC_INTERFACE_CONFORMANCE, SQLUINTEGER, SQL_OIC_CORE)
			CASE_NUM(SQL_ASYNC_MODE, SQLUINTEGER, SQL_AM_NONE)
			CASE_NUM(SQL_ASYNC_NOTIFICATION, SQLUINTEGER, SQL_ASYNC_NOTIFICATION_NOT_CAPABLE)
			CASE_NUM(SQL_DEFAULT_TXN_ISOLATION, SQLUINTEGER, SQL_TXN_SERIALIZABLE)
			CASE_NUM(SQL_DRIVER_AWARE_POOLING_SUPPORTED, SQLUINTEGER, SQL_DRIVER_AWARE_POOLING_CAPABLE)

			/// USMALLINT одиночные значения
			CASE_NUM(SQL_GROUP_BY, SQLUSMALLINT, SQL_GB_NO_RELATION)
			CASE_NUM(SQL_CATALOG_LOCATION, SQLUSMALLINT, SQL_CL_START)
			CASE_NUM(SQL_FILE_USAGE, SQLUSMALLINT, SQL_FILE_NOT_SUPPORTED)
			CASE_NUM(SQL_IDENTIFIER_CASE, SQLUSMALLINT, SQL_IC_SENSITIVE)
			CASE_NUM(SQL_QUOTED_IDENTIFIER_CASE, SQLUSMALLINT, SQL_IC_SENSITIVE)
			CASE_NUM(SQL_CONCAT_NULL_BEHAVIOR, SQLUSMALLINT, SQL_CB_NULL)
			CASE_NUM(SQL_CORRELATION_NAME, SQLUSMALLINT, SQL_CN_ANY)
			CASE_FALLTHROUGH(SQL_CURSOR_COMMIT_BEHAVIOR)
			CASE_NUM(SQL_CURSOR_ROLLBACK_BEHAVIOR, SQLUSMALLINT, SQL_CB_PRESERVE)
			CASE_NUM(SQL_CURSOR_SENSITIVITY, SQLUSMALLINT, SQL_INSENSITIVE)

			/// UINTEGER непустые битмаски
			CASE_NUM(SQL_CATALOG_USAGE, SQLUINTEGER, SQL_CU_DML_STATEMENTS | SQL_CU_TABLE_DEFINITION)
			CASE_NUM(SQL_AGGREGATE_FUNCTIONS, SQLUINTEGER, SQL_AF_ALL | SQL_AF_AVG | SQL_AF_COUNT | SQL_AF_DISTINCT | SQL_AF_MAX | SQL_AF_MIN | SQL_AF_SUM)
			CASE_NUM(SQL_ALTER_TABLE, SQLUINTEGER, SQL_AT_ADD_COLUMN_DEFAULT | SQL_AT_ADD_COLUMN_SINGLE | SQL_AT_DROP_COLUMN_DEFAULT | SQL_AT_SET_COLUMN_DEFAULT)
			CASE_NUM(SQL_CONVERT_FUNCTIONS, SQLUINTEGER, SQL_FN_CVT_CAST | SQL_FN_CVT_CONVERT)
			CASE_NUM(SQL_CREATE_TABLE, SQLUINTEGER, SQL_CT_CREATE_TABLE)
			CASE_NUM(SQL_CREATE_VIEW, SQLUINTEGER, SQL_CV_CREATE_VIEW)
			CASE_NUM(SQL_DROP_TABLE, SQLUINTEGER, SQL_DT_DROP_TABLE)
			CASE_NUM(SQL_DROP_VIEW, SQLUINTEGER, SQL_DT_DROP_VIEW)
			CASE_NUM(SQL_DATETIME_LITERALS, SQLUINTEGER, SQL_DL_SQL92_DATE | SQL_DL_SQL92_TIMESTAMP)
			CASE_NUM(SQL_GETDATA_EXTENSIONS, SQLUINTEGER, SQL_GD_ANY_COLUMN | SQL_GD_ANY_ORDER | SQL_GD_BOUND)
			CASE_NUM(SQL_INDEX_KEYWORDS, SQLUINTEGER, SQL_IK_NONE)

			CASE_FALLTHROUGH(SQL_CONVERT_BIGINT)
			CASE_FALLTHROUGH(SQL_CONVERT_BINARY)
			CASE_FALLTHROUGH(SQL_CONVERT_BIT)
			CASE_FALLTHROUGH(SQL_CONVERT_CHAR)
			CASE_FALLTHROUGH(SQL_CONVERT_GUID)
			CASE_FALLTHROUGH(SQL_CONVERT_DATE)
			CASE_FALLTHROUGH(SQL_CONVERT_DECIMAL)
			CASE_FALLTHROUGH(SQL_CONVERT_DOUBLE)
			CASE_FALLTHROUGH(SQL_CONVERT_FLOAT)
			CASE_FALLTHROUGH(SQL_CONVERT_INTEGER)
			CASE_FALLTHROUGH(SQL_CONVERT_INTERVAL_YEAR_MONTH)
			CASE_FALLTHROUGH(SQL_CONVERT_INTERVAL_DAY_TIME)
			CASE_FALLTHROUGH(SQL_CONVERT_LONGVARBINARY)
			CASE_FALLTHROUGH(SQL_CONVERT_LONGVARCHAR)
			CASE_FALLTHROUGH(SQL_CONVERT_NUMERIC)
			CASE_FALLTHROUGH(SQL_CONVERT_REAL)
			CASE_FALLTHROUGH(SQL_CONVERT_SMALLINT)
			CASE_FALLTHROUGH(SQL_CONVERT_TIME)
			CASE_FALLTHROUGH(SQL_CONVERT_TIMESTAMP)
			CASE_FALLTHROUGH(SQL_CONVERT_TINYINT)
			CASE_FALLTHROUGH(SQL_CONVERT_VARBINARY)
			CASE_NUM(SQL_CONVERT_VARCHAR, SQLUINTEGER,
				SQL_CVT_BIGINT | SQL_CVT_BINARY | SQL_CVT_BIT |  SQL_CVT_GUID | SQL_CVT_CHAR |  SQL_CVT_DATE | SQL_CVT_DECIMAL | SQL_CVT_DOUBLE
				| SQL_CVT_FLOAT | SQL_CVT_INTEGER /*| SQL_CVT_INTERVAL_YEAR_MONTH | SQL_CVT_INTERVAL_DAY_TIME*/ | SQL_CVT_LONGVARBINARY
				| SQL_CVT_LONGVARCHAR | SQL_CVT_NUMERIC | SQL_CVT_REAL | SQL_CVT_SMALLINT | SQL_CVT_TIME | SQL_CVT_TIMESTAMP | SQL_CVT_TINYINT
				| SQL_CVT_VARBINARY | SQL_CVT_VARCHAR)

			/// UINTEGER пустые битмаски
			CASE_FALLTHROUGH(SQL_OWNER_USAGE)
			CASE_FALLTHROUGH(SQL_ALTER_DOMAIN)
			CASE_FALLTHROUGH(SQL_BATCH_ROW_COUNT)
			CASE_FALLTHROUGH(SQL_BATCH_SUPPORT)
			CASE_FALLTHROUGH(SQL_BOOKMARK_PERSISTENCE)
			CASE_FALLTHROUGH(SQL_CREATE_ASSERTION)
			CASE_FALLTHROUGH(SQL_CREATE_CHARACTER_SET)
			CASE_FALLTHROUGH(SQL_CREATE_COLLATION)
			CASE_FALLTHROUGH(SQL_CREATE_DOMAIN)
			CASE_FALLTHROUGH(SQL_CREATE_SCHEMA)
			CASE_FALLTHROUGH(SQL_CREATE_TRANSLATION)
			CASE_FALLTHROUGH(SQL_DROP_ASSERTION)
			CASE_FALLTHROUGH(SQL_DROP_CHARACTER_SET)
			CASE_FALLTHROUGH(SQL_DROP_COLLATION)
			CASE_FALLTHROUGH(SQL_DROP_DOMAIN)
			CASE_FALLTHROUGH(SQL_DROP_SCHEMA)
			CASE_FALLTHROUGH(SQL_DROP_TRANSLATION)
			CASE_FALLTHROUGH(SQL_DYNAMIC_CURSOR_ATTRIBUTES1)
			CASE_FALLTHROUGH(SQL_DYNAMIC_CURSOR_ATTRIBUTES2)
			CASE_FALLTHROUGH(SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1)
			CASE_FALLTHROUGH(SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2)
			CASE_NUM(SQL_DDL_INDEX, SQLUINTEGER, 0)

			/// Ограничения на максимальное число.
			CASE_FALLTHROUGH(SQL_ACTIVE_ENVIRONMENTS)

			/// Ограничения на максимальное число.
			CASE_FALLTHROUGH(SQL_MAX_COLUMNS_IN_SELECT)
			CASE_FALLTHROUGH(SQL_MAX_DRIVER_CONNECTIONS)
			CASE_FALLTHROUGH(SQL_MAX_CONCURRENT_ACTIVITIES)
			CASE_FALLTHROUGH(SQL_MAX_COLUMN_NAME_LEN)
			CASE_FALLTHROUGH(SQL_MAX_CURSOR_NAME_LEN)
			CASE_FALLTHROUGH(SQL_MAX_SCHEMA_NAME_LEN)
			CASE_FALLTHROUGH(SQL_MAX_CATALOG_NAME_LEN)
			CASE_FALLTHROUGH(SQL_MAX_TABLE_NAME_LEN)
			CASE_FALLTHROUGH(SQL_MAX_COLUMNS_IN_GROUP_BY)
			CASE_FALLTHROUGH(SQL_MAX_COLUMNS_IN_INDEX)
			CASE_FALLTHROUGH(SQL_MAX_COLUMNS_IN_ORDER_BY)
			CASE_FALLTHROUGH(SQL_MAX_COLUMNS_IN_TABLE)
			CASE_FALLTHROUGH(SQL_MAX_INDEX_SIZE)
			CASE_FALLTHROUGH(SQL_MAX_ROW_SIZE)
			CASE_FALLTHROUGH(SQL_MAX_STATEMENT_LEN)
			CASE_FALLTHROUGH(SQL_MAX_TABLES_IN_SELECT)
			CASE_FALLTHROUGH(SQL_MAX_USER_NAME_LEN)
				return fillOutputNumber(uint32_t(0), out_info_value, out_info_value_max_length, out_info_value_length);

			default:
				throw std::runtime_error("Unsupported info type: " + Poco::NumberFormatter::format(info_type));
		}
	});
}


RETCODE SQL_API
SQLPrepare(HSTMT statement_handle,
		   SQLCHAR * statement_text, SQLINTEGER statement_text_size)
{
	LOG(__FUNCTION__);

	return doWith<Statement>(statement_handle, [&](Statement & statement)
	{
		if (!statement.query.empty())
			throw std::runtime_error("Prepare called, but statement query is not empty.");

		statement.query = stringFromSQLChar(statement_text, statement_text_size);
		if (statement.query.empty())
			throw std::runtime_error("Prepare called with empty query.");

		LOG(statement.query);
		return SQL_SUCCESS;
	});
}


RETCODE SQL_API
SQLExecute(HSTMT statement_handle)
{
	LOG(__FUNCTION__);

	return doWith<Statement>(statement_handle, [&](Statement & statement)
	{
		statement.sendRequest();
		return SQL_SUCCESS;
	});
}


RETCODE SQL_API
SQLExecDirect(HSTMT statement_handle,
			  SQLCHAR * statement_text, SQLINTEGER statement_text_size)
{
	LOG(__FUNCTION__);

	RETCODE ret = SQLPrepare(statement_handle, statement_text, statement_text_size);
	if (ret != SQL_SUCCESS)
		return ret;

	return SQLExecute(statement_handle);
}


RETCODE SQL_API
SQLNumResultCols(HSTMT statement_handle,
				 SQLSMALLINT * column_count)
{
	LOG(__FUNCTION__);

	return doWith<Statement>(statement_handle, [&](Statement & statement)
	{
		*column_count = statement.result.getNumColumns();
		LOG(*column_count);
		return SQL_SUCCESS;
 	});
}


RETCODE SQL_API
SQLColAttribute(HSTMT statement_handle, SQLUSMALLINT column_number, SQLUSMALLINT field_identifier,
	SQLPOINTER out_string_value, SQLSMALLINT out_string_value_max_size, SQLSMALLINT * out_string_value_size,
	SQLLEN * out_num_value)
{
	LOG(__FUNCTION__);

	return doWith<Statement>(statement_handle, [&](Statement & statement) -> RETCODE
	{
		if (column_number < 1 || column_number > statement.result.getNumColumns())
			throw std::runtime_error("Column number is out of range.");

		size_t column_idx = column_number - 1;

		SQLLEN num_value = 0;
		std::string str_value;

		switch (field_identifier)
		{
			case SQL_DESC_AUTO_UNIQUE_VALUE:
				break;
			case SQL_DESC_BASE_COLUMN_NAME:
				break;
			case SQL_DESC_BASE_TABLE_NAME:
				break;
			case SQL_DESC_CASE_SENSITIVE:
				num_value = SQL_TRUE;
				break;
			case SQL_DESC_CATALOG_NAME:
				break;
			case SQL_DESC_CONCISE_TYPE:
				// TODO
				break;
			case SQL_DESC_COUNT:
				num_value = statement.result.getNumColumns();
				break;
			case SQL_DESC_DISPLAY_SIZE:
				num_value = statement.result.getColumnInfo(column_idx).display_size;
				break;
			case SQL_DESC_FIXED_PREC_SCALE:
				break;
			case SQL_DESC_LABEL:
				str_value = statement.result.getColumnInfo(column_idx).name;
				break;
			case SQL_DESC_LENGTH:
				break;
			case SQL_DESC_LITERAL_PREFIX:
				break;
			case SQL_DESC_LITERAL_SUFFIX:
				break;
			case SQL_DESC_LOCAL_TYPE_NAME:
				break;
			case SQL_DESC_NAME:
				str_value = statement.result.getColumnInfo(column_idx).name;
				break;
			case SQL_DESC_NULLABLE:
				num_value = SQL_FALSE;
				break;
			case SQL_DESC_NUM_PREC_RADIX:
				break;
			case SQL_DESC_OCTET_LENGTH:
				break;
			case SQL_DESC_PRECISION:
				break;
			case SQL_DESC_SCALE:
				break;
			case SQL_DESC_SCHEMA_NAME:
				break;
			case SQL_DESC_SEARCHABLE:
				break;
			case SQL_DESC_TABLE_NAME:
				break;
			case SQL_DESC_TYPE:
				break;
			case SQL_DESC_TYPE_NAME:
				break;
			case SQL_DESC_UNNAMED:
				num_value = SQL_NAMED;
				break;
			case SQL_DESC_UNSIGNED:
				num_value = statement.connection.environment.types_info.at(statement.result.getColumnInfo(column_idx).type).is_unsigned;
				break;
			case SQL_DESC_UPDATABLE:
				num_value = SQL_FALSE;
				break;
			default:
				return SQL_ERROR;
		}

		if (out_num_value)
			*out_num_value = num_value;

		return fillOutputString(str_value, out_string_value, out_string_value_max_size, out_string_value_size);
	});
}


RETCODE SQL_API
SQLFetch(HSTMT statement_handle)
{
	LOG(__FUNCTION__);

	return doWith<Statement>(statement_handle, [&](Statement & statement)
	{
		bool res = statement.fetchRow();
		return res ? SQL_SUCCESS : SQL_NO_DATA;
	});
}


RETCODE SQL_API
SQLGetData(HSTMT statement_handle,
		   SQLUSMALLINT column_or_param_number, SQLSMALLINT target_type,
		   PTR out_value, SQLLEN out_value_max_size,
		   SQLLEN * out_value_size_or_indicator)
{
	LOG(__FUNCTION__);

	return doWith<Statement>(statement_handle, [&](Statement & statement)
	{
		if (column_or_param_number < 1 || column_or_param_number > statement.result.getNumColumns())
			throw std::runtime_error("Column number is out of range.");

		size_t column_idx = column_or_param_number - 1;

		LOG("column: " << column_idx << ", target_type: " << target_type);

		const Field & field = statement.current_row.data[column_idx];

		switch (target_type)
		{
			case SQL_ARD_TYPE:
			case SQL_C_DEFAULT:
				throw std::runtime_error("Unsupported type requested.");

			case SQL_C_WCHAR:
			case SQL_C_CHAR:
			{
				if (target_type == SQL_C_CHAR)
				{
					return fillOutputString(field.data.data(), field.data.size(), out_value, out_value_max_size, out_value_size_or_indicator);
				}
				else
				{
					std::string converted;

					converted.resize(field.data.size() * 2 + 1, '\xFF');
					converted[field.data.size() * 2] = '\0';
					for (size_t i = 0, size = field.data.size(); i < size; ++i)
						converted[i * 2] = field.data[i];

					return fillOutputString(converted.data(), converted.size(), out_value, out_value_max_size, out_value_size_or_indicator);
				}
				break;
			}

			case SQL_C_TINYINT:
			case SQL_C_STINYINT:
				return fillOutputNumber<int8_t>(field.getInt(), out_value, out_value_max_size, out_value_size_or_indicator);

			case SQL_C_UTINYINT:
				return fillOutputNumber<uint8_t>(field.getUInt(), out_value, out_value_max_size, out_value_size_or_indicator);

			case SQL_C_SHORT:
			case SQL_C_SSHORT:
				return fillOutputNumber<int16_t>(field.getInt(), out_value, out_value_max_size, out_value_size_or_indicator);

			case SQL_C_USHORT:
				return fillOutputNumber<uint16_t>(field.getUInt(), out_value, out_value_max_size, out_value_size_or_indicator);

			case SQL_C_LONG:
			case SQL_C_SLONG:
				return fillOutputNumber<int32_t>(field.getInt(), out_value, out_value_max_size, out_value_size_or_indicator);

			case SQL_C_ULONG:
				return fillOutputNumber<uint32_t>(field.getUInt(), out_value, out_value_max_size, out_value_size_or_indicator);

			case SQL_C_SBIGINT:
				return fillOutputNumber<int64_t>(field.getInt(), out_value, out_value_max_size, out_value_size_or_indicator);

			case SQL_C_UBIGINT:
				return fillOutputNumber<uint64_t>(field.getUInt(), out_value, out_value_max_size, out_value_size_or_indicator);

			case SQL_C_FLOAT:
				return fillOutputNumber<float>(field.getFloat(), out_value, out_value_max_size, out_value_size_or_indicator);

			case SQL_C_DOUBLE:
				return fillOutputNumber<double>(field.getDouble(), out_value, out_value_max_size, out_value_size_or_indicator);

			default:
				throw std::runtime_error("Unknown type requested.");
		}
	});
}


RETCODE SQL_API
SQLRowCount(HSTMT statement_handle,
			SQLLEN * out_row_count)
{
	LOG(__FUNCTION__);

	return doWith<Statement>(statement_handle, [&](Statement & statement)
	{
		if (out_row_count)
			*out_row_count = statement.result.getNumRows();
		return SQL_SUCCESS;
	});
}


RETCODE SQL_API
SQLMoreResults(HSTMT hstmt)
{
	LOG(__FUNCTION__);

	return SQL_NO_DATA;
}


RETCODE SQL_API
SQLDisconnect(HDBC connection_handle)
{
	LOG(__FUNCTION__);

	return doWith<Connection>(connection_handle, [&](Connection & connection)
	{
		connection.session.reset();
		return SQL_SUCCESS;
	});
}


RETCODE SQL_API
SQLSetEnvAttr(SQLHENV environment_handle, SQLINTEGER attribute,
    SQLPOINTER value, SQLINTEGER value_length)
{
	LOG(__FUNCTION__);

	return doWith<Environment>(environment_handle, [&](Environment & environment)
	{
		LOG("attr: " << attribute);

		switch (attribute)
		{
			case SQL_ATTR_CONNECTION_POOLING:
			case SQL_ATTR_CP_MATCH:
			case SQL_ATTR_OUTPUT_NTS:
			default:
				throw std::runtime_error("Unsupported environment attribute.");

			case SQL_ATTR_ODBC_VERSION:
				intptr_t int_value = reinterpret_cast<intptr_t>(value);
				if (int_value != SQL_OV_ODBC2 && int_value != SQL_OV_ODBC3)
					throw std::runtime_error("Unsupported ODBC version.");

				environment.odbc_version = int_value;
				LOG("Set ODBC version to " << int_value);

				return SQL_SUCCESS;
		}
	});
}


RETCODE SQL_API
SQLGetEnvAttr(SQLHENV environment_handle, SQLINTEGER attribute,
    SQLPOINTER out_value, SQLINTEGER out_value_max_length, SQLINTEGER * out_value_length)
{
	LOG(__FUNCTION__);

	return doWith<Environment>(environment_handle, [&](Environment & environment)
	{
		LOG("attr: " << attribute);

		switch (attribute)
		{
			case SQL_ATTR_CONNECTION_POOLING:
			case SQL_ATTR_CP_MATCH:
			case SQL_ATTR_OUTPUT_NTS:
			default:
				throw std::runtime_error("Unsupported environment attribute.");

			case SQL_ATTR_ODBC_VERSION:
				*reinterpret_cast<intptr_t*>(out_value) = environment.odbc_version;
				if (out_value_length)
					*out_value_length = sizeof(SQLUINTEGER);

				return SQL_SUCCESS;
		}
	});
}


RETCODE SQL_API
SQLSetConnectAttr(SQLHDBC connection_handle, SQLINTEGER attribute,
        SQLPOINTER value, SQLINTEGER value_length)
{
	LOG(__FUNCTION__);

	return doWith<Connection>(connection_handle, [&](Connection & connection)
	{
		LOG("attr: " << attribute);

		switch (attribute)
		{
			case SQL_ATTR_LOGIN_TIMEOUT:
			{
				auto timeout = static_cast<SQLUSMALLINT>(reinterpret_cast<intptr_t>(value));
				LOG("Timeout: " << timeout);
				connection.session.setTimeout(Poco::Timespan(timeout));
				break;
			}

			case SQL_ATTR_ACCESS_MODE:
			case SQL_ATTR_ASYNC_ENABLE:
			case SQL_ATTR_AUTO_IPD:
			case SQL_ATTR_AUTOCOMMIT:
			case SQL_ATTR_CONNECTION_DEAD:
			case SQL_ATTR_CONNECTION_TIMEOUT:
			case SQL_ATTR_CURRENT_CATALOG:
			case SQL_ATTR_METADATA_ID:
			case SQL_ATTR_ODBC_CURSORS:
			case SQL_ATTR_PACKET_SIZE:
			case SQL_ATTR_QUIET_MODE:
			case SQL_ATTR_TRACE:
			case SQL_ATTR_TRACEFILE:
			case SQL_ATTR_TRANSLATE_LIB:
			case SQL_ATTR_TRANSLATE_OPTION:
			case SQL_ATTR_TXN_ISOLATION:
			default:
				throw std::runtime_error("Unsupported connection attribute.");
		}

		return SQL_SUCCESS;
	});
}


RETCODE SQL_API
SQLGetDiagRec(SQLSMALLINT handle_type, SQLHANDLE handle,
    SQLSMALLINT record_number,
	SQLCHAR * out_sqlstate,
	SQLINTEGER * out_native_error_code,
	SQLCHAR * out_mesage, SQLSMALLINT out_message_max_size, SQLSMALLINT * out_message_size)
{
	LOG(__FUNCTION__);

	LOG("handle_type: " << handle_type << ", record_number: " << record_number << ", out_message_max_size: " << out_message_max_size);

	if (nullptr == handle)
		return SQL_INVALID_HANDLE;

	if (record_number <= 0 || out_message_max_size < 0)
		return SQL_ERROR;

	if (record_number > 1)
		return SQL_NO_DATA;

	DiagnosticRecord * diagnostic_record = nullptr;
	switch (handle_type)
	{
		case SQL_HANDLE_ENV:
			diagnostic_record = &reinterpret_cast<Environment *>(handle)->diagnostic_record;
			break;
		case SQL_HANDLE_DBC:
			diagnostic_record = &reinterpret_cast<Connection *>(handle)->diagnostic_record;
			break;
		case SQL_HANDLE_STMT:
			diagnostic_record = &reinterpret_cast<Statement *>(handle)->diagnostic_record;
			break;
		default:
			return SQL_ERROR;
	}

	if (diagnostic_record->native_error_code == 0)
		return SQL_NO_DATA;

	/// Пятибуквенный SQLSTATE и завершающий ноль.
	if (out_sqlstate)
		strncpy(reinterpret_cast<char *>(out_sqlstate), diagnostic_record->sql_state.data(), 6);

	if (out_native_error_code)
		*out_native_error_code = diagnostic_record->native_error_code;

	return fillOutputString(diagnostic_record->message, out_mesage, out_message_max_size, out_message_size);
}


RETCODE SQL_API
SQLGetDiagField(SQLSMALLINT handle_type, SQLHANDLE handle,
    SQLSMALLINT record_number,
	SQLSMALLINT field_id,
	SQLPOINTER out_mesage, SQLSMALLINT out_message_max_size, SQLSMALLINT * out_message_size)
{
	LOG(__FUNCTION__);

	return SQLGetDiagRec(
		handle_type,
		handle,
		record_number,
		nullptr,
		nullptr,
		reinterpret_cast<SQLCHAR *>(out_mesage),
		out_message_max_size,
		out_message_size);
}


RETCODE SQL_API
SQLTables(HSTMT statement_handle,
		  SQLCHAR * catalog_name, SQLSMALLINT catalog_name_length,
		  SQLCHAR * schema_name, SQLSMALLINT schema_name_length,
		  SQLCHAR * table_name, SQLSMALLINT table_name_length,
		  SQLCHAR * table_type, SQLSMALLINT table_type_length)
{
	LOG(__FUNCTION__);

	return doWith<Statement>(statement_handle, [&](Statement & statement)
	{
		LOG(
			stringFromSQLChar(catalog_name, catalog_name_length)
			<< ", " << stringFromSQLChar(schema_name, schema_name_length)
			<< ", " << stringFromSQLChar(table_name, table_name_length)
			<< ", " << stringFromSQLChar(table_type, table_type_length));

		statement.query = "SELECT"
				" 'TABLE' AS TABLE_TYPE,"
				" '' AS TABLE_CAT,"
				" database AS TABLE_SCHEM,"
				" name AS TABLE_NAME,"
				" '' AS REMARKS"
			" FROM system.tables"
			" ORDER BY  TABLE_TYPE, TABLE_CAT, TABLE_SCHEM, TABLE_NAME";

		statement.sendRequest();
		return SQL_SUCCESS;
	});
}


RETCODE SQL_API
SQLBrowseConnect(HDBC connection_handle,
				 SQLCHAR *szConnStrIn,
				 SQLSMALLINT cbConnStrIn,
				 SQLCHAR *szConnStrOut,
				 SQLSMALLINT cbConnStrOutMax,
				 SQLSMALLINT *pcbConnStrOut)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLBindCol(HSTMT StatementHandle,
		   SQLUSMALLINT ColumnNumber, SQLSMALLINT TargetType,
		   PTR TargetValue, SQLLEN BufferLength,
		   SQLLEN *StrLen_or_Ind)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLCancel(HSTMT StatementHandle)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLColumns(HSTMT StatementHandle,
		   SQLCHAR *CatalogName, SQLSMALLINT NameLength1,
		   SQLCHAR *SchemaName, SQLSMALLINT NameLength2,
		   SQLCHAR *TableName, SQLSMALLINT NameLength3,
		   SQLCHAR *ColumnName, SQLSMALLINT NameLength4)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLDataSources(HENV EnvironmentHandle,
			   SQLUSMALLINT Direction, SQLCHAR *ServerName,
			   SQLSMALLINT BufferLength1, SQLSMALLINT *NameLength1,
			   SQLCHAR *Description, SQLSMALLINT BufferLength2,
			   SQLSMALLINT *NameLength2)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLDescribeCol(HSTMT StatementHandle,
			   SQLUSMALLINT ColumnNumber, SQLCHAR *ColumnName,
			   SQLSMALLINT BufferLength, SQLSMALLINT *NameLength,
			   SQLSMALLINT *DataType, SQLULEN *ColumnSize,
			   SQLSMALLINT *DecimalDigits, SQLSMALLINT *Nullable)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLGetCursorName(HSTMT StatementHandle,
				 SQLCHAR *CursorName, SQLSMALLINT BufferLength,
				 SQLSMALLINT *NameLength)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


/*
/// Эта функция может быть реализована в driver manager-е.
RETCODE SQL_API
SQLGetFunctions(HDBC ConnectionHandle,
				SQLUSMALLINT FunctionId, SQLUSMALLINT *Supported)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}*/


RETCODE SQL_API
SQLGetTypeInfo(HSTMT StatementHandle,
			   SQLSMALLINT DataType)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLParamData(HSTMT StatementHandle,
			 PTR *Value)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLPutData(HSTMT StatementHandle,
		   PTR Data, SQLLEN StrLen_or_Ind)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLSetCursorName(HSTMT StatementHandle,
				 SQLCHAR *CursorName, SQLSMALLINT NameLength)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLSetParam(HSTMT StatementHandle,
			SQLUSMALLINT ParameterNumber, SQLSMALLINT ValueType,
			SQLSMALLINT ParameterType, SQLULEN LengthPrecision,
			SQLSMALLINT ParameterScale, PTR ParameterValue,
			SQLLEN *StrLen_or_Ind)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLSpecialColumns(HSTMT StatementHandle,
				  SQLUSMALLINT IdentifierType, SQLCHAR *CatalogName,
				  SQLSMALLINT NameLength1, SQLCHAR *SchemaName,
				  SQLSMALLINT NameLength2, SQLCHAR *TableName,
				  SQLSMALLINT NameLength3, SQLUSMALLINT Scope,
				  SQLUSMALLINT Nullable)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLStatistics(HSTMT StatementHandle,
			  SQLCHAR *CatalogName, SQLSMALLINT NameLength1,
			  SQLCHAR *SchemaName, SQLSMALLINT NameLength2,
			  SQLCHAR *TableName, SQLSMALLINT NameLength3,
			  SQLUSMALLINT Unique, SQLUSMALLINT Reserved)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLColumnPrivileges(HSTMT hstmt,
					SQLCHAR *szCatalogName,
					SQLSMALLINT cbCatalogName,
					SQLCHAR *szSchemaName,
					SQLSMALLINT cbSchemaName,
					SQLCHAR *szTableName,
					SQLSMALLINT cbTableName,
					SQLCHAR *szColumnName,
					SQLSMALLINT cbColumnName)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLDescribeParam(HSTMT hstmt,
				 SQLUSMALLINT ipar,
				 SQLSMALLINT *pfSqlType,
				 SQLULEN *pcbParamDef,
				 SQLSMALLINT *pibScale,
				 SQLSMALLINT *pfNullable)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLExtendedFetch(HSTMT hstmt,
				 SQLUSMALLINT fFetchType,
				 SQLLEN irow,
#if defined(WITH_UNIXODBC) && (SIZEOF_LONG != 8)
				 SQLROWSETSIZE *pcrow,
#else
				 SQLULEN *pcrow,
#endif /* WITH_UNIXODBC */
				 SQLUSMALLINT *rgfRowStatus)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLForeignKeys(HSTMT hstmt,
			   SQLCHAR *szPkCatalogName,
			   SQLSMALLINT cbPkCatalogName,
			   SQLCHAR *szPkSchemaName,
			   SQLSMALLINT cbPkSchemaName,
			   SQLCHAR *szPkTableName,
			   SQLSMALLINT cbPkTableName,
			   SQLCHAR *szFkCatalogName,
			   SQLSMALLINT cbFkCatalogName,
			   SQLCHAR *szFkSchemaName,
			   SQLSMALLINT cbFkSchemaName,
			   SQLCHAR *szFkTableName,
			   SQLSMALLINT cbFkTableName)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLNativeSql(HDBC hdbc,
			 SQLCHAR *szSqlStrIn,
			 SQLINTEGER cbSqlStrIn,
			 SQLCHAR *szSqlStr,
			 SQLINTEGER cbSqlStrMax,
			 SQLINTEGER *pcbSqlStr)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLNumParams(HSTMT hstmt,
			 SQLSMALLINT *pcpar)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLPrimaryKeys(HSTMT hstmt,
			   SQLCHAR *szCatalogName,
			   SQLSMALLINT cbCatalogName,
			   SQLCHAR *szSchemaName,
			   SQLSMALLINT cbSchemaName,
			   SQLCHAR *szTableName,
			   SQLSMALLINT cbTableName)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLProcedureColumns(HSTMT hstmt,
					SQLCHAR *szCatalogName,
					SQLSMALLINT cbCatalogName,
					SQLCHAR *szSchemaName,
					SQLSMALLINT cbSchemaName,
					SQLCHAR *szProcName,
					SQLSMALLINT cbProcName,
					SQLCHAR *szColumnName,
					SQLSMALLINT cbColumnName)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLProcedures(HSTMT hstmt,
			  SQLCHAR *szCatalogName,
			  SQLSMALLINT cbCatalogName,
			  SQLCHAR *szSchemaName,
			  SQLSMALLINT cbSchemaName,
			  SQLCHAR *szProcName,
			  SQLSMALLINT cbProcName)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLSetPos(HSTMT hstmt,
		  SQLSETPOSIROW irow,
		  SQLUSMALLINT fOption,
		  SQLUSMALLINT fLock)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLTablePrivileges(HSTMT hstmt,
				   SQLCHAR *szCatalogName,
				   SQLSMALLINT cbCatalogName,
				   SQLCHAR *szSchemaName,
				   SQLSMALLINT cbSchemaName,
				   SQLCHAR *szTableName,
				   SQLSMALLINT cbTableName)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLBindParameter(HSTMT hstmt,
				 SQLUSMALLINT ipar,
				 SQLSMALLINT fParamType,
				 SQLSMALLINT fCType,
				 SQLSMALLINT fSqlType,
				 SQLULEN cbColDef,
				 SQLSMALLINT ibScale,
				 PTR rgbValue,
				 SQLLEN cbValueMax,
				 SQLLEN *pcbValue)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}

/*
RETCODE SQL_API
SQLBulkOperations(
     SQLHSTMT       StatementHandle,
     SQLUSMALLINT   Operation)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}*/


RETCODE SQL_API
SQLCancelHandle(
      SQLSMALLINT  HandleType,
      SQLHANDLE    Handle)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLCloseCursor(
	SQLHSTMT     StatementHandle)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLCompleteAsync(
      SQLSMALLINT HandleType,
      SQLHANDLE   Handle,
      RETCODE *   AsyncRetCodePtr)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLCopyDesc(
     SQLHDESC     SourceDescHandle,
     SQLHDESC     TargetDescHandle)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLEndTran(
     SQLSMALLINT   HandleType,
     SQLHANDLE     Handle,
     SQLSMALLINT   CompletionType)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLError(SQLHENV hDrvEnv, SQLHDBC hDrvDbc, SQLHSTMT hDrvStmt,
    SQLCHAR *szSqlState, SQLINTEGER *pfNativeError, SQLCHAR *szErrorMsg,
    SQLSMALLINT nErrorMsgMax, SQLSMALLINT *pcbErrorMsg)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLFetchScroll(SQLHSTMT hDrvStmt, SQLSMALLINT nOrientation, SQLLEN nOffset)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLGetConnectAttr(SQLHDBC hDrvDbc, SQLINTEGER Attribute, SQLPOINTER Value,
    SQLINTEGER BufferLength, SQLINTEGER *StringLength)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLGetConnectOption(SQLHDBC hDrvDbc, UWORD fOption, PTR pvParam)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLGetDescField(SQLHDESC DescriptorHandle, SQLSMALLINT RecordNumber,
    SQLSMALLINT FieldIdentifier, SQLPOINTER Value, SQLINTEGER BufferLength,
    SQLINTEGER *StringLength)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLGetDescRec(SQLHDESC DescriptorHandle, SQLSMALLINT RecordNumber,
    SQLCHAR *Name, SQLSMALLINT BufferLength, SQLSMALLINT *StringLength,
    SQLSMALLINT *Type, SQLSMALLINT *SubType, SQLLEN *Length,
    SQLSMALLINT *Precision, SQLSMALLINT *Scale, SQLSMALLINT *Nullable)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLGetStmtAttr(SQLHSTMT hDrvStmt, SQLINTEGER Attribute, SQLPOINTER Value,
    SQLINTEGER BufferLength, SQLINTEGER *StringLength)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLGetStmtOption(SQLHSTMT hDrvStmt, UWORD fOption, PTR pvParam)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLParamOptions(SQLHSTMT hDrvStmt, SQLULEN nRow, SQLULEN *pnRow)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLSetConnectOption(SQLHDBC hDrvDbc, UWORD nOption, SQLULEN vParam)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLSetDescField(SQLHDESC DescriptorHandle, SQLSMALLINT RecordNumber,
    SQLSMALLINT FieldIdentifier, SQLPOINTER Value, SQLINTEGER BufferLength)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLSetDescRec(SQLHDESC hDescriptorHandle, SQLSMALLINT nRecordNumber,
    SQLSMALLINT nType, SQLSMALLINT nSubType, SQLLEN nLength,
    SQLSMALLINT nPrecision, SQLSMALLINT nScale, SQLPOINTER pData,
    SQLLEN *pnStringLength, SQLLEN *pnIndicator)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLSetScrollOptions(
	SQLHSTMT hDrvStmt, SQLUSMALLINT fConcurrency, SQLLEN crowKeyset,
    SQLUSMALLINT crowRowset)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLSetStmtAttr(SQLHSTMT hDrvStmt, SQLINTEGER Attribute, SQLPOINTER Value,
    SQLINTEGER StringLength)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLSetStmtOption(SQLHSTMT hDrvStmt, UWORD fOption, SQLULEN vParam)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


RETCODE SQL_API
SQLTransact(SQLHENV hDrvEnv, SQLHDBC hDrvDbc, UWORD nType)
{
	LOG(__FUNCTION__);
	return SQL_ERROR;
}


}