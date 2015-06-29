#include "pdns/utility.hh"
#include <sstream>
#include "sodbc.hh"
#include <string.h>

static void testResult( SQLRETURN result, SQLSMALLINT type, SQLHANDLE handle, const std::string & message )
{
  // cerr<<"result = "<<result<<endl;
  if ( result == SQL_SUCCESS) // FIXME: testing only? || result == SQL_SUCCESS_WITH_INFO )
    return;

  ostringstream errmsg;

  errmsg << message << ": ";

  if ( result != SQL_ERROR && result != SQL_SUCCESS_WITH_INFO ) {
    cerr<<"handle "<<handle<<" got result "<<result<<endl;
    errmsg << "SQL function returned "<<result<<", no additional information available"<<endl;
    throw SSqlException( errmsg.str() );
  }

  SQLINTEGER i = 0;
  SQLINTEGER native;
  SQLCHAR state[ 7 ];
  SQLCHAR text[256];
  SQLSMALLINT len;
  SQLRETURN ret;

  do
  {
    // cerr<<"getting sql diag record "<<i<<endl;
    ret = SQLGetDiagRec(type, handle, ++i, state, &native, text,
    sizeof(text), &len );
    // cerr<<"getdiagrec said "<<ret<<endl;
    if (SQL_SUCCEEDED(ret)) { // cerr<<"got it"<<endl;
      errmsg<<state<<i<<native<<text<<"/";
    }
  }
  while( ret == SQL_SUCCESS );
  throw SSqlException( errmsg.str() );
}

class SODBCStatement: public SSqlStatement
{
public:
  SODBCStatement(const string& query, bool dolog, int nparams, SQLHDBC connection)
  {
    SQLRETURN result;

    d_query = query;
    // d_nparams = nparams;
    d_conn = connection;
    d_dolog = dolog;
    d_residx = 0;
    d_paridx = 0;

    // Allocate statement handle.
    result = SQLAllocHandle( SQL_HANDLE_STMT, d_conn, &d_statement );
    testResult( result, SQL_HANDLE_DBC, d_conn, "Could not allocate a statement handle." );

    result = SQLPrepare(d_statement, (SQLCHAR *) query.c_str(), SQL_NTS);
    testResult( result, SQL_HANDLE_STMT, d_statement, "Could not prepare query." );

    SQLSMALLINT paramcount;
    result = SQLNumParams(d_statement, &paramcount);
    testResult( result, SQL_HANDLE_STMT, d_statement, "Could not get parameter count." );

    if (paramcount != nparams)
      throw SSqlException("Provided parameter count does not match statement: " + d_query);

    d_parnum = nparams;
    // cerr<<"prepared ("<<query<<")"<<endl;
  }

  typedef struct {
    SQLPOINTER      ParameterValuePtr;
    SQLLEN*         LenPtr;
  } ODBCParam;

  vector<ODBCParam> d_req_bind;

  SSqlStatement* bind(const string& name, bool value) { return bind(name, (long)value); }
  SSqlStatement* bind(const string& name, long value) {

    // cerr<<"asked to bind long "<<value<<endl;
    // cerr<<"d_req_bind.size()="<<d_req_bind.size()<<endl;
    // cerr<<"d_parnum="<<d_parnum<<endl;

    if(d_req_bind.size() > (d_parnum+1)) throw SSqlException("Trying to bind too many parameters.");

    ODBCParam p;

    p.ParameterValuePtr = new long[1];
    *((long*)p.ParameterValuePtr) = value;
    p.LenPtr = new SQLLEN;
    *(p.LenPtr) = sizeof(long);

    d_req_bind.push_back(p);

    SQLRETURN result = SQLBindParameter(
      d_statement,           // StatementHandle,
      d_paridx+1,            // ParameterNumber,
      SQL_PARAM_INPUT,       // InputOutputType,
      SQL_C_SLONG,           // ValueType,
      SQL_BIGINT,            // ParameterType,
      0,                     // ColumnSize,
      0,                     // DecimalDigits,
      p.ParameterValuePtr,   // ParameterValuePtr,
      0,                     // BufferLength,
      p.LenPtr               // StrLen_or_IndPtr
    );
    testResult( result, SQL_HANDLE_STMT, d_statement, "Could not bind parameter.");
    d_paridx++;

    return this;
  }
  SSqlStatement* bind(const string& name, uint32_t value) { return bind(name, (long)value); }
  SSqlStatement* bind(const string& name, int value) { return bind(name, (long)value); }
  SSqlStatement* bind(const string& name, unsigned long value) { return bind(name, (long)value);}
  SSqlStatement* bind(const string& name, long long value) { return bind(name, (long)value); };
  SSqlStatement* bind(const string& name, unsigned long long value) { return bind(name, (long)value); }
  SSqlStatement* bind(const string& name, const std::string& value) {

    // cerr<<"asked to bind string "<<value<<endl;

    if(d_req_bind.size() > (d_parnum+1)) throw SSqlException("Trying to bind too many parameters.");

    ODBCParam p;

    p.ParameterValuePtr = (char*) new char[value.size()+1];
    value.copy((char*)p.ParameterValuePtr, value.size());
    ((char*)p.ParameterValuePtr)[value.size()]=0;
    p.LenPtr=new SQLLEN;
    *(p.LenPtr)=value.size();

    d_req_bind.push_back(p);

    SQLRETURN result = SQLBindParameter(
      d_statement,           // StatementHandle,
      d_paridx+1,            // ParameterNumber,
      SQL_PARAM_INPUT,       // InputOutputType,
      SQL_C_CHAR,            // ValueType,
      SQL_VARCHAR,           // ParameterType,
      value.size(),          // ColumnSize,
      0,                     // DecimalDigits,
      p.ParameterValuePtr,   // ParameterValuePtr,
      value.size()+1,        // BufferLength,
      p.LenPtr               // StrLen_or_IndPtr
    );
    testResult( result, SQL_HANDLE_STMT, d_statement, "Binding parameter.");
    d_paridx++;

    return this;
  }

  SSqlStatement* bindNull(const string& name) {
    if(d_req_bind.size() > (d_parnum+1)) throw SSqlException("Trying to bind too many parameters.");

    ODBCParam p;

    p.ParameterValuePtr = NULL;
    p.LenPtr=new SQLLEN;
    *(p.LenPtr)=SQL_NULL_DATA;

    d_req_bind.push_back(p);

    SQLRETURN result = SQLBindParameter(
      d_statement,           // StatementHandle,
      d_paridx+1,            // ParameterNumber,
      SQL_PARAM_INPUT,       // InputOutputType,
      SQL_C_CHAR,            // ValueType,
      SQL_VARCHAR,           // ParameterType,
      0,                     // ColumnSize,
      0,                     // DecimalDigits,
      p.ParameterValuePtr,   // ParameterValuePtr,
      0,                     // BufferLength,
      p.LenPtr               // StrLen_or_IndPtr
    );
    testResult( result, SQL_HANDLE_STMT, d_statement, "Binding parameter.");
    d_paridx++;

    return this;
  }

  SSqlStatement* execute()
  {
    SQLRETURN result;
    // cerr<<"execute("<<d_query<<")"<<endl;
    if (d_dolog) {
      // L<<Logger::Warning<<"Query: "<<d_query<<endl;
    }

    result = SQLExecute(d_statement);
    if(result != SQL_NO_DATA)  // odbc+sqlite returns this on 'no rows updated'
        testResult( result, SQL_HANDLE_STMT, d_statement, "Could not execute query ("+d_query+")." );

    // Determine the number of columns.
    result = SQLNumResultCols( d_statement, &m_columncount );
    testResult( result, SQL_HANDLE_STMT, d_statement, "Could not determine the number of columns." );
    // cerr<<"got "<<m_columncount<<" columns"<<endl;

    if(m_columncount) {
      // cerr<<"first SQLFetch"<<endl;
      d_result = SQLFetch(d_statement);
      // cerr<<"first SQLFetch done, d_result="<<d_result<<endl;
    }
    else
      d_result = SQL_NO_DATA;

    if(d_result != SQL_NO_DATA)
        testResult( d_result, SQL_HANDLE_STMT, d_statement, "Could not do first SQLFetch for ("+d_query+")." );
    return this;
  }

  bool hasNextRow() {
    // cerr<<"hasNextRow d_result="<<d_result<<endl;
    return d_result!=SQL_NO_DATA;
  }
  SSqlStatement* nextRow(row_t& row);

  SSqlStatement* getResult(result_t& result) {
    result.clear();
    // if (d_res == NULL) return this;
    row_t row;
    while(hasNextRow()) { nextRow(row); result.push_back(row); }
    return this;
  }

  SSqlStatement* reset() {
    SQLCloseCursor(d_statement); // hack, this probably violates some state transitions

    for(auto &i: d_req_bind) { delete [] (char*) i.ParameterValuePtr; delete i.LenPtr; }
    d_req_bind.clear();
    d_residx = 0;
    d_paridx = 0;
    return this;
  }
  const std::string& getQuery() { return d_query; }

private:
  string d_query;
  bool d_dolog;
  bool d_havenextrow;
  int d_residx, d_paridx, d_parnum;
  SQLRETURN d_result;

  SQLHDBC d_conn;
  SQLHSTMT d_statement;    //!< Database statement handle.

  //! Column type.
  struct column_t
  {
    SQLSMALLINT m_type;       //!< Type of the column.
    SQLULEN     m_size;       //!< Column size.
    SQLPOINTER  m_pData;      //!< Pointer to the memory where to store the data.
    bool        m_canBeNull;  //!< Can this column be null?
  };

  //! Column info.
  SQLSMALLINT m_columncount;

};

SSqlStatement* SODBCStatement::nextRow(row_t& row)
{
  SQLRETURN result;

  row.clear();

  result = d_result;
  // cerr<<"at start of nextRow, previous SQLFetch result is "<<result<<endl;
  // FIXME handle errors (SQL_NO_DATA==100, anything other than the two SUCCESS options below is bad news)
  if ( result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO )
  {
    // cerr<<"got row"<<endl;
    // We've got a data row, now lets get the results.
    SQLLEN len;
    for ( int i = 0; i < m_columncount; i++ )
    {
      // Clear buffer.
      // cerr<<"clearing m_pData of size "<<m_columnInfo[ i ].m_size<<endl;
      SQLCHAR         coldata[128*1024];

      // FIXME: because we cap m_size to 128kbyte, this can truncate silently. see Retrieving Variable-Length Data in Parts at https://msdn.microsoft.com/en-us/library/ms715441(v=vs.85).aspx
      result = SQLGetData( d_statement, i + 1, SQL_C_CHAR, (SQLPOINTER) coldata, 128*1024-1, &len );
      // cerr<<"len="<<len<<endl;
      testResult( result, SQL_HANDLE_STMT, d_statement, "Could not get data." );
      if ( len == SQL_NULL_DATA ) {
        // Column is NULL, so we can skip the converting part.
        row.push_back( "" );
      }
      else
      {
        row.push_back(reinterpret_cast<char*>(coldata)); // FIXME: not NUL-safe, use len
      }
    }

    // Done!
    d_residx++;
    // cerr<<"SQLFetch"<<endl;
    d_result = SQLFetch(d_statement);
    // cerr<<"subsequent SQLFetch done, d_result="<<d_result<<endl;
    if(d_result == SQL_NO_DATA) {
      SQLRETURN result = SQLMoreResults(d_statement);
      // cerr<<"SQLMoreResults done, result="<<d_result<<endl;
      if (result == SQL_NO_DATA) {
        d_result = result;
      }
      else {
        testResult( result, SQL_HANDLE_STMT, d_statement, "Could not fetch next result set for ("+d_query+").");
      d_result = SQLFetch(d_statement);
      }
    }
    testResult( result, SQL_HANDLE_STMT, d_statement, "Could not do subsequent SQLFetch for ("+d_query+")." );

    return this;
  }

  SQLFreeStmt( d_statement, SQL_CLOSE );
  throw SSqlException( "Should not get here." );
  return this;
}

// Constructor.
SODBC::SODBC(
             const std::string & dsn,
             const std::string & username,
             const std::string & password
            )
{
  SQLRETURN     result;

  // Allocate an environment handle.
  result = SQLAllocHandle( SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_environment );
  testResult( result, SQL_NULL_HANDLE, NULL, "Could not allocate an environment handle." );

  // Set ODBC version. (IEUW!)
  result = SQLSetEnvAttr( m_environment, SQL_ATTR_ODBC_VERSION, reinterpret_cast< void * >( SQL_OV_ODBC3 ), 0 );
  testResult( result, SQL_HANDLE_ENV, m_environment, "Could not set the ODBC version." );

  // Allocate connection handle.
  result = SQLAllocHandle( SQL_HANDLE_DBC, m_environment, &m_connection );
  testResult( result, SQL_HANDLE_ENV, m_environment, "Could not allocate a connection handle." );

  // Connect to the database.
  char *l_dsn       = strdup( dsn.c_str());
  char *l_username  = strdup( username.c_str());
  char *l_password  = strdup( password.c_str());

  result = SQLConnect( m_connection,
    reinterpret_cast< SQLTCHAR * >( l_dsn ), dsn.length(),
    reinterpret_cast< SQLTCHAR * >( l_username ), username.length(),
    reinterpret_cast< SQLTCHAR * >( l_password ), password.length());

  free( l_dsn );
  free( l_username );
  free( l_password );

  testResult( result, SQL_HANDLE_DBC, m_connection, "Could not connect to ODBC datasource." );


  m_busy  = false;
  m_log   = false;
}


// Destructor.
SODBC::~SODBC( void )
{
  // Disconnect from database and free all used resources.
  // SQLFreeHandle( SQL_HANDLE_STMT, m_statement );

  SQLDisconnect( m_connection );

  SQLFreeHandle( SQL_HANDLE_DBC, m_connection );
  SQLFreeHandle( SQL_HANDLE_ENV, m_environment );

  // Free all allocated column memory.
  // for ( int i = 0; i < m_columnInfo.size(); i++ )
  // {
  //   if ( m_columnInfo[ i ].m_pData )
  //     delete m_columnInfo[ i ].m_pData;
  // }
}

// Executes a command.
void SODBC::execute( const std::string & command )
{
  SQLRETURN   result;
  SODBCStatement stmt(command, false, 0, m_connection);

  stmt.execute()->reset();
}

// Sets the log state.
void SODBC::setLog( bool state )
{
  m_log = state;
}

// Returns an exception.
SSqlException SODBC::sPerrorException( const std::string & reason )
{
  return SSqlException( reason );
}

SSqlStatement* SODBC::prepare(const string& query, int nparams)
{
  return new SODBCStatement(query, true, nparams, m_connection);
}


void SODBC::startTransaction() {
  // cerr<<"starting transaction"<<endl;
  SQLRETURN result;
  result = SQLSetConnectAttr(m_connection, SQL_ATTR_AUTOCOMMIT, SQL_AUTOCOMMIT_OFF, 0);
  testResult( result, SQL_HANDLE_DBC, m_connection, "startTransaction (enable autocommit) failed" );
}

void SODBC::commit() {
  // cerr<<"commit!"<<endl;
  SQLRETURN result;

  result = SQLEndTran(SQL_HANDLE_DBC, m_connection, SQL_COMMIT); // don't really need this, AUTOCOMMIT_OFF below will also commit
  testResult( result, SQL_HANDLE_DBC, m_connection, "commit failed" );

  result = SQLSetConnectAttr(m_connection, SQL_ATTR_AUTOCOMMIT, SQL_AUTOCOMMIT_OFF, 0);
  testResult( result, SQL_HANDLE_DBC, m_connection, "disabling autocommit after commit failed" );
}

void SODBC::rollback() {
  // cerr<<"rollback!"<<endl;
  SQLRETURN result;

  result = SQLEndTran(SQL_HANDLE_DBC, m_connection, SQL_ROLLBACK);
  testResult( result, SQL_HANDLE_DBC, m_connection, "rollback failed" );

  result = SQLSetConnectAttr(m_connection, SQL_ATTR_AUTOCOMMIT, SQL_AUTOCOMMIT_OFF, 0);
  testResult( result, SQL_HANDLE_DBC, m_connection, "disabling autocommit after rollback failed" );
}
