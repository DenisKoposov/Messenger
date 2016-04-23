#include <Server.hpp>
#include <boost/bind.hpp>

Connection::Connection( Server& server ) :
    _myServer( server ),
    _socket( server.service() ),
    _wasOnline( std::chrono::system_clock::now() ),
    _loggedIn( false ),
    _started( false )
{
}

void Connection::start()
{
    std::cout << "Starting new connection" << std::endl;

    _started = true;
    doReadSize();
}

void Connection::doReadSize()
{
    std::cout << "Reading size..." << std::endl;

    boost::asio::async_read( _socket, boost::asio::buffer( _readBuffer ),
    boost::bind( &Connection::doRead, shared_from_this(), _1, _2 ) );
}

void Connection::doRead( const boost::system::error_code& err, size_t bytes )
{
    std::cout << "Reading..." << bytes << std::endl;

    if ( err )
    {
        //stop();
        return;
    }

    int size = 0;

    std::copy( _readBuffer, _readBuffer + 8, &size);

    if ( size <= 0 )
    {
        //stop();
        return;
    }

    std::cout << size << std::endl;

    _strReadBuffer.resize( size );
    boost::asio::async_read( _socket, boost::asio::buffer( _strReadBuffer, size ),
    //boost::bind( &Connection::readComplete, shared_from_this(), _1, _2 ),
    boost::bind( &Connection::onRead, shared_from_this(), _1, _2 ) );
}

//size_t Connection::readComplete( const boost::system::error_code& err, size_t bytes ) // not used
//{
//    std::cout << std::string( _readBuffer ) << std::endl;
//
//    if ( err )
//        return 0 ;
//
//    bool found = std::find( _readBuffer, _readBuffer + bytes, '\n') < _readBuffer + bytes;
//
//    if ( found == true )
//        return 0;
//    else
//        return 1;
//}

void Connection::onRead( const boost::system::error_code& err, size_t bytes )
{
    std::cout << "On read..." << bytes << std::endl;

//	if ( err )
//	{
//        stop();
//        return;
//    }

    std::string msg( _strReadBuffer.data() );
    std::cout << "From client: " << msg << std::endl;

    Stanza st;
    st.load( std::string( _strReadBuffer.data() ) );

    if ( ( _loggedIn == false ) && ( st.getStanzaType() != Stanza::IQ ||
         ( st.getSubType() != Stanza::SIGNIN && st.getSubType() != Stanza::SIGNUP ) ) )
    {
        stop();
        return;
    }

    switch ( st.getStanzaType() )
    {
        case Stanza::MESSAGE:
            onMessage( st );
            break;

        case Stanza::PRESENCE:
            onPresence();
            break;

        case Stanza::IQ:
            onRequest( st );
            break;

        case Stanza::ROASTER:
            onRoaster();
            break;

        default:
            break;
    }
}

void Connection::onMessage( Stanza st )
{
    std::string target = st.getTo().getNode();

    if ( _myServer.checkAccount( target ) == false )
    {
        Stanza ans;
        ans.setStanzaType( Stanza::IQ );
        ans.setSubType( Stanza::ERROR );
        std::string strAns;
        ans.save( strAns );

        doWriteQuick( strAns );
        return;
    }

    resend( st );
}

void Connection::store()
{
    if ( _received.empty() )
        return;

    Server::Data& storage = std::get<1>(*(_myServer.accounts().find( _login ))) ;

    while ( !_received.empty() )
    {
        storage.newMessages.push( _received.front() );
        _received.pop_front();
    }
}

void Connection::retrieve()
{
    Server::Data& storage = std::get<1>(*(_myServer.accounts().find( _login ))) ;

    while ( !storage.newMessages.empty() )
    {
        _received.push_front( storage.newMessages.front() );
        storage.newMessages.pop();
    }
}

void Connection::resend( Stanza st )
{
    std::get<1>(*(_myServer.connections().find( _login )) )->_received.push_back( st );
}

void Connection::onPresence()
{
    updateRecentOnlineTime();
    doReadSize();
}

void Connection::onRequest( Stanza st )
{
    updateRecentOnlineTime();

    switch ( st.getSubType() )
    {
        case Stanza::SIGNIN:
        {
            onLogin( st );
            break;
        }
        case Stanza::SIGNUP:
        {
            onRegister( st );
            break;
        }
        case Stanza::GET:
        {
            Stanza ev;
            ev.setStanzaType( Stanza::EVENT );
            ev.setSubType( Stanza::BEGIN );
            _received.push_front( ev );
            ev.setSubType( Stanza::END );
            _received.push_back( ev );

            doWrite();
            break;
        }
        default:
        {
            break;
        }
    }
}

void Connection::onRoaster()
{
    updateRecentOnlineTime();
    Stanza ans;
    std::map<std::string, Connection::ptr>::iterator it;
    std::string strAns;

    for ( it = _myServer.connections().begin();
          it != _myServer.connections().end(); ++it )
    {
        JID newJID;
        newJID.setNode( _login );
        ans.addAvailable( newJID );
    }

    ans.setStanzaType( Stanza::ROASTER );
    ans.save( strAns );

    doWriteQuick( strAns );
}

void Connection::onLogin( Stanza st )
{
    std::string body  = st.getMSG();
    std::string login = body.substr( 0, body.find("\n") );
    std::string psswd = body.substr( body.find("\n") );

    if ( _myServer.checkLoginAndPassword( login, psswd ) == true )
    {
        _login = login;
        _loggedIn = true;
        _myServer.addConnection( shared_from_this() );
        retrieve();
        Stanza ans;
        ans.setStanzaType( Stanza::IQ );
        ans.setSubType( Stanza::AVAILABLE );
        std::string strAns;
        ans.save( strAns );

        doWriteQuick( strAns );
    } else
    {
        Stanza ans;
        ans.setStanzaType( Stanza::IQ );
        ans.setSubType( Stanza::ERROR );
        std::string strAns;
        ans.save( strAns );

        doWriteQuick( strAns );
    }
}

void Connection::onRegister( Stanza st )
{
    std::string body = st.getMSG();
    std::string login = body.substr( 0, body.find("\n") );
    std::string psswd = body.substr( body.find("\n") );

    if ( _myServer.checkAccount( login ) == true )
    {
        Stanza ans;
        ans.setStanzaType( Stanza::IQ );
        ans.setSubType( Stanza::ERROR );
        std::string strAns;
        ans.save( strAns );

        doWriteQuick( strAns );
    } else
    {
        _myServer.addAccount( login, psswd );

        Stanza ans;
        ans.setStanzaType( Stanza::IQ );
        ans.setSubType( Stanza::AVAILABLE );
        std::string strAns;
        ans.save( strAns );

        doWriteQuick( strAns );
    }
}

void Connection::doWriteQuick( std::string msg )
{
    std::cout << "Do write quick..." << std::endl;

	if ( !_started )
        return;

    _strWriteBuffer.resize( msg.size() );
    std::copy( msg.begin(), msg.end(), _strWriteBuffer.begin() );

	_socket.async_write_some( boost::asio::buffer( _strWriteBuffer, _strWriteBuffer.size() ),
	boost::bind( &Connection::onWriteQuick, shared_from_this(), _1, _2 ) );
}

void Connection::onWriteQuick( const boost::system::error_code& err, size_t bytes )
{
//    if ( err )
//        stop();

    std::cout << "On quick write..." << std::endl;
    doReadSize();
}

void Connection::doWrite()
{
    std::cout << "Do write..." << std::endl;

	if ( !_started )
        return;

    std::string msg;
    _received.front().save( msg );
    _received.pop_front();

    _strWriteBuffer.resize( msg.size() );
    std::copy( msg.begin(), msg.end(), _strWriteBuffer.begin() );

	_socket.async_write_some( boost::asio::buffer( _strWriteBuffer, _strWriteBuffer.size() ),
	boost::bind( &Connection::onWrite, shared_from_this(), _1, _2 ) );
}

void Connection::onWrite( const boost::system::error_code& err, size_t bytes )
{
//    if ( err )
//    {
//        stop();
//        return;
//    }
    std::cout << "On write..." << std::endl;

    if ( _received.empty() == false )
        doWrite();

    doReadSize();
}

void Connection::stop()
{
    std::cout << "Closing connection..." << std::endl;

    if ( !_started )
        return;

    _socket.close();
    _started = false;
    _loggedIn = false;
    store();
    _myServer.deleteConnection( _login );
}

