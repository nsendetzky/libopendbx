#include "config.hpp"
#include "commands.hpp"
#include "completion.hpp"
#include "odbx-sql.hpp"
#include <opendbx/api>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <string>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <locale.h>
#include <libintl.h>
#include <readline/readline.h>
#include <readline/history.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif



using namespace OpenDBX;

using std::string;
using std::cout;
using std::cerr;
using std::endl;


// hack to access completion
Completion* g_comp = NULL;

char* complete( const char* text, int state )
{
	char* match = NULL;

	if( g_comp != NULL )
	{
		if( state == 0 ) {
			g_comp->find( text );
		}

		if( ( match = (char*) g_comp->get() ) !=  NULL ) {
			return strdup( match );
		}
	}

	return match;
}


const string help( const string& filename )
{
	std::ostringstream help;

	help << gettext( "Read and execute semicolon-terminated SQL statements from stdin" ) << endl;
	help << endl;
	help << gettext( "Usage: " ) << filename << " " << " -c <configfile> [-d <delimiter>] [-h] [-i] [-k <keywordfile>] [-s <separator>]" << endl;
	help << endl;
	help << "    -c      " << gettext( "read configuration from file" ) << endl;
	help << "    -d      " << gettext( "start/end delimiter for fields in output" ) << endl;
	help << "    -h      " << gettext( "print help" ) << endl;
	help << "    -i      " << gettext( "interactive mode" ) << endl;
	help << "    -k      " << gettext( "SQL keyword file" ) << endl;
	help << "    -s      " << gettext( "separator between fields in output" ) << endl;

	return help.str();
}



void output( Result& result, struct format* fparam )
{
	odbxres stat;
	unsigned long fields;

	while( ( stat = result.getResult( NULL, 25 ) ) != ODBX_RES_DONE )
	{
		switch( stat )
		{
			case ODBX_RES_TIMEOUT:
			case ODBX_RES_NOROWS:
				continue;
			default:
				break;
		}

		fields = result.columnCount();

		if( fparam->header == true && fields > 0 )
		{
			cout << fparam->delimiter << result.columnName( 0 ) << fparam->delimiter;

			for( unsigned long i = 1; i < fields; i++ )
			{
				cout << fparam->separator << fparam->delimiter << result.columnName( i ) << fparam->delimiter;
			}
			cout << endl << "---" << endl;
		}

		while( result.getRow() != ODBX_ROW_DONE )
		{
			if( fields > 0 )
			{
				if( result.fieldValue( 0 ) == NULL ) { cout << "NULL"; }
				else { cout << fparam->delimiter << result.fieldValue( 0 ) << fparam->delimiter; }

				for( unsigned long i = 1; i < fields; i++ )
				{
					if( result.fieldValue( i ) == NULL ) { cout << fparam->separator << "NULL"; }
					else { cout << fparam->separator << fparam->delimiter << result.fieldValue( i ) << fparam->delimiter; }
				}
			}
			cout << endl;
		}
	}
}



void loopstmts( Conn& conn, struct format* fparam, bool iactive )
{
	char* line;
	size_t len;
	string sql;
	const char* fprompt = "";
	const char* cprompt = "";
	Commands cmd( conn );


	if( iactive )
	{
		cout << gettext( "Interactive SQL shell, use .help to list available commands" ) << endl;
		fprompt = "sql> ";
		cprompt = "  -> ";
		using_history();
	}

	while( ( line = readline( fprompt ) ) != NULL )
	{
		len = strlen( line );
		if( len == 0 ) { free( line ); continue;}
		if( line[0] == '.' ) { cmd.exec( string( line ), fparam ); continue; }

		sql = string( line, len );
		free( line );

		if( sql[len-1] != ';' )
		{
			while( ( line = readline( cprompt ) ) != NULL )
			{
				len = strlen( line );
				sql += "\n" + string( line, len );
				free( line );

				if( sql[sql.size()-1] == ';' ) { break; }
			}
		}

		if( iactive ) { add_history( sql.c_str() ); }
		sql.erase( sql.size()-1, 1 );

		try
		{
			Stmt stmt = conn.create( sql );
			Result result = stmt.execute();

			output( result, fparam );
		}
		catch( OpenDBX::Exception& oe )
		{
			cerr << gettext( "Warning: " ) << oe.what() << endl;
			if( oe.getType() < 0 ) { return; }
		}
	}
}



int main( int argc, char* argv[] )
{
	try
	{
		int param;
		bool iactive = false;
		char* conffile = NULL;
		char* keywordfile = NULL;
		struct format fparam;


		setlocale( LC_ALL, "" );
		textdomain( "opendbx-utils" );
		bindtextdomain( "opendbx-utils", LOCALEDIR );

		fparam.delimiter = "";
		fparam.separator = "\t";
		fparam.header = false;

		cfg_opt_t opts[] =
		{
			CFG_STR( "backend", "mysql", CFGF_NONE ),
			CFG_STR( "host", "localhost", CFGF_NONE ),
			CFG_STR( "port", "", CFGF_NONE ),
			CFG_STR( "database", "", CFGF_NONE ),
			CFG_STR( "username", "", CFGF_NONE ),
			CFG_STR( "password", "", CFGF_NONE ),
			CFG_END()
		};

		while( ( param = getopt( argc, argv, "c:d:hik:s:" ) ) != -1 )
		{
			switch( param )
			{
				case 'c':
					conffile = optarg;
					break;
				case 'd':
					fparam.delimiter = optarg;
					break;
				case 'h':
					cout << help( string( argv[0] ) ) << endl;
					return 0;
				case 'i':
					iactive = true;
					fparam.header = true;
					break;
				case 'k':
					keywordfile = optarg;
					break;
				case 's':
					fparam.separator = optarg;
					break;
				default:
					const char tmp = (char) param;
					throw std::runtime_error( "Unknown option '" + string( &tmp ) + "' with value '" + string( optarg ) + "'" );
			}
		}

		if( keywordfile == NULL ) {
			keywordfile = KEYWORDFILE;
		}

		Config cfg( conffile, opts );
		g_comp = new Completion( keywordfile );

		rl_completion_entry_function = &complete;

		Conn conn( cfg.getStr( "backend" ), cfg.getStr( "host" ), cfg.getStr( "port" ) );
		conn.bind( cfg.getStr( "database" ), cfg.getStr( "username" ), cfg.getStr( "password" ), ODBX_BIND_SIMPLE );

		loopstmts( conn, &fparam, iactive );

		delete g_comp;
	}
	catch( OpenDBX::Exception& oe )
	{
		cerr << gettext( "Error: " ) << oe.what() << endl;
		return 1;
	}
	catch( ConfigException& ce )
	{
		cerr << gettext( "Error: " ) << ce.what() << endl;
		cerr << help( string( argv[0] ) ) << endl;
		return 1;
	}
	catch( runtime_error& e )
	{
		cerr << gettext( "Error: " ) << e.what() << endl;
		return 1;
	}
	catch( ... )
	{
		cerr << gettext( "Error: Caught unknown exception" ) << endl;
		return 1;
	}

	return 0;
}
