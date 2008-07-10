/* Copyright (C) 2008 AbiSource Corporation B.V.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef __TLS_TUNNEL_H__
#define __TLS_TUNNEL_H__

#ifndef WIN32
#include <pthread.h>
#else
#include <windows.h>
#endif

#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>
#include <gnutls/gnutls.h>
#include <asio.hpp>
#include <boost/bind.hpp>
#include <string>
#include <vector>
#include <gcrypt.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#define return_val_if_neg(C, val) { if (C < 0) {return val;} }

namespace tls_tunnel {

#define MIN_CLIENT_PORT 50000
#define MAX_CLIENT_PORT 50100

#define TUNNEL_BUFFER_SIZE 4096
#define LOCAL_BUFFER_SIZE 4096

#define TRANSPORT_ERROR "Transport exception: "
#define TLS_SETUP_ERROR "Error setting up TLS connection"
#define TLS_CREDENTIALS_ERROR "Error setting up TLS connection"
#define TLS_VERIFY_PEER_ERROR "Error verifying peer"
#define TLS_VERIFY_HOSTNAME_ERROR "Error verifying hostname"
#define TLS_CANT_GET_PEER_CERT_ERROR "Failed to get peer certificate"

typedef boost::shared_ptr<asio::ip::tcp::socket> socket_ptr_t;
typedef boost::shared_ptr<gnutls_session_t> session_ptr_t;

class mutex {
public:
	mutex()	{
#ifdef WIN32
		repr = CreateMutex(0, FALSE, 0);
#else
		pthread_mutex_init(&repr, NULL);
#endif
	}

	void lock() {
#ifdef WIN32
		WaitForSingleObject(repr, INFINITE);
#else
		pthread_mutex_lock(&repr);
#endif	
	}
	
	void unlock() {
#ifdef WIN32
		ReleaseMutex(repr);
#else
		pthread_mutex_unlock(&repr);
#endif	
	}

	~mutex()
	{
#ifdef WIN32
		CloseHandle(repr);
#else
		pthread_mutex_destroy(&repr);
#endif
	}

private:
	mutex( const mutex& );
	const mutex& operator=( const mutex& );

#ifdef WIN32
	HANDLE repr;
#else
	pthread_mutex_t repr;
#endif
};

static int gcry_tunnel_mutex_init(void **priv)
{
	if (!priv)
		return -1;
	*priv = new mutex();
	return 0;
}

static int gcry_tunnel_mutex_destroy(void **priv)
{
	if (!priv || !*priv)
		return -1;
	delete reinterpret_cast<mutex*>(*priv);
	return 0;
}

static int gcry_tunnel_mutex_lock(void **priv)
{
	reinterpret_cast<mutex*>(*priv)->lock();
	return 0;
}

static int gcry_tunnel_mutex_unlock(void **priv)
{
	reinterpret_cast<mutex*>(*priv)->unlock();
	return 0;
}

static struct gcry_thread_cbs gcry_threads_tunnel =
{ GCRY_THREAD_OPTION_USER, NULL,
  gcry_tunnel_mutex_init, gcry_tunnel_mutex_destroy,
  gcry_tunnel_mutex_lock, gcry_tunnel_mutex_unlock,
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

static bool tls_tunnel_init() {
	if (gcry_control(GCRYCTL_SET_THREAD_CBS, &tls_tunnel::gcry_threads_tunnel) != 0)
		return false;
	if (gnutls_global_init() != 0)
		return false;
	return true;
}

static void tls_tunnel_deinit() {
	gnutls_global_deinit();
}

class Exception {
public:
	Exception(const std::string& message)
		: message_(message)
	{
	}
	
	const std::string& message() const {
		return message_;
	}
	
private:
	std::string message_;
};

// private class; should't be in the public api
class _SessionPtrDestuctor {
public:
	void operator()(gnutls_session_t* session) {
		if (!session || !*session)
			return;
		gnutls_deinit(*session);			
		delete session;
	}
};

class Transport {
public:
	asio::io_service& io_service() {
		return io_service_;
	}

	void run() {
		io_service_.run();
	}

	void stop() {
		io_service_.stop();
	}

protected:
	Transport()
		: io_service_(),
		work_(io_service_)
	{
	}
	
private:
	asio::io_service io_service_;
	asio::io_service::work work_;
};

class ClientTransport : public Transport {
public:
	ClientTransport(const std::string& host, unsigned short port, boost::function<void (socket_ptr_t)> on_connect)
		: Transport(),
		host_(host),
		port_(port),
		on_connect_(on_connect)
	{
	}
	
	void connect() {
		asio::ip::tcp::resolver resolver(io_service());
		asio::ip::tcp::resolver::query query(host_, boost::lexical_cast<std::string>(port_));
		asio::ip::tcp::resolver::iterator iterator(resolver.resolve(query));
		socket_ptr_t socket_ptr(new asio::ip::tcp::socket(io_service()));
		socket_ptr->connect(*iterator);
		on_connect_(socket_ptr);
	}

private:
	std::string host_;
	unsigned short port_;
	boost::function<void (socket_ptr_t)> on_connect_;
};


class ServerTransport : public Transport {
public:
	ServerTransport(const std::string& ip, unsigned short port, boost::function<void (socket_ptr_t)> on_connect) 
		: Transport(),
		acceptor_(io_service(), asio::ip::tcp::endpoint(asio::ip::address_v4::from_string(ip), port)),
		on_connect_(on_connect)
	{
	}

	void accept() {
		socket_ptr_t socket_ptr(new asio::ip::tcp::socket(io_service()));
		acceptor_.async_accept(*socket_ptr, boost::bind(&ServerTransport::on_accept, this, asio::placeholders::error, socket_ptr));
	}

private:
	void on_accept(const asio::error_code& error, socket_ptr_t socket_ptr) {
		if (error) {
			return;
		}
		on_connect_(socket_ptr);
		accept();
	}

	asio::ip::tcp::acceptor acceptor_;
	boost::function<void (socket_ptr_t)> on_connect_;
};

static ssize_t read(gnutls_transport_ptr_t ptr, void* buffer, size_t size) {
	asio::ip::tcp::socket* socket = reinterpret_cast<asio::ip::tcp::socket*>(ptr);
	try {
		return asio::read(*socket, asio::buffer(buffer, size));
	} catch (asio::system_error& se) {
		return -1;
	}
}

static ssize_t write(gnutls_transport_ptr_t ptr, const void* buffer, size_t size) {
	asio::ip::tcp::socket* socket = reinterpret_cast<asio::ip::tcp::socket*>(ptr);
	try {
		return asio::write(*socket, asio::buffer(buffer, size));
	} catch (asio::system_error& se) {
		return -1;
	}
}

class Proxy {
public:
	virtual ~Proxy() {
		gnutls_certificate_free_credentials(x509cred);
	}

	void run() {
		transport().run();
	}

	void stop() {
		transport().stop();
	}

	virtual Transport& transport() = 0;
	
protected:
	Proxy(const std::string& ca_file)
		: tunnel_buffer(TUNNEL_BUFFER_SIZE),
		local_buffer(LOCAL_BUFFER_SIZE)
	{
		// setup certificates
		if (gnutls_certificate_allocate_credentials(&x509cred) < 0)
			throw Exception(TLS_SETUP_ERROR);
		if (gnutls_certificate_set_x509_trust_file(x509cred, ca_file.c_str(), GNUTLS_X509_FMT_PEM) < 0)
			throw Exception(TLS_SETUP_ERROR);
	}

	void on_local_read(const asio::error_code& error, std::size_t bytes_transferred,
			session_ptr_t session_ptr, socket_ptr_t local_socket_ptr, socket_ptr_t remote_socket_ptr) {
		if (error) {
			disconnect_(session_ptr, local_socket_ptr, remote_socket_ptr);
			return;
		}

		// write the data to the tunnel connection
		if (gnutls_record_send(*session_ptr, &local_buffer[0], bytes_transferred) < 0) {
			disconnect_(session_ptr, local_socket_ptr, remote_socket_ptr);
			return;
		}
		
		local_socket_ptr->async_receive(
				asio::buffer(&local_buffer[0], local_buffer.size()),
				boost::bind(&Proxy::on_local_read, this, asio::placeholders::error, asio::placeholders::bytes_transferred,
						session_ptr, local_socket_ptr, remote_socket_ptr)
			);
	}
	
	void tunnel(session_ptr_t session_ptr, socket_ptr_t local_socket_ptr, socket_ptr_t remote_socket_ptr) {
		asio::thread thread(boost::bind(&Proxy::tunnel_, this, session_ptr, local_socket_ptr, remote_socket_ptr));
	}

	void disconnect_(session_ptr_t session_ptr, socket_ptr_t local_socket_ptr, socket_ptr_t remote_socket_ptr) {
		// shutdown the tls session (ignore any error condition)
		if (session_ptr)
			gnutls_bye(*session_ptr, GNUTLS_SHUT_RDWR);

		// shutdown the sockets belonging to this tunnel
		asio::error_code ec;
		if (local_socket_ptr) {
			local_socket_ptr->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
			local_socket_ptr->close(ec);
		}
		if (remote_socket_ptr) {	
			remote_socket_ptr->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
			remote_socket_ptr->close(ec);
		}
	}

	gnutls_certificate_credentials_t x509cred;	

private:
	void tunnel_(session_ptr_t session_ptr, socket_ptr_t local_socket_ptr, socket_ptr_t remote_socket_ptr) {
		local_socket_ptr->async_receive(
				asio::buffer(&local_buffer[0], local_buffer.size()),
				boost::bind(&Proxy::on_local_read, this, asio::placeholders::error, asio::placeholders::bytes_transferred, 
						session_ptr, local_socket_ptr, remote_socket_ptr)
			);
		
		ssize_t bytes_transferred = 0;
		while (true) {
			bytes_transferred = gnutls_record_recv(*session_ptr, &tunnel_buffer[0], tunnel_buffer.size());
			
			if (bytes_transferred == 0)
				break;
				
			// TODO: check return code properly?
			if (bytes_transferred < 0)
				break;
			
			// forward the data over the local connection
			try {
				asio::write(*local_socket_ptr, asio::buffer(&tunnel_buffer[0], bytes_transferred));
			} catch (asio::system_error& se) {
				break;
			}
		}

		disconnect_(session_ptr, local_socket_ptr, remote_socket_ptr);		
	}

	std::vector<char> tunnel_buffer;
	std::vector<char> local_buffer;
};

static const int PRIORITIES[] = { GNUTLS_KX_ANON_DH, GNUTLS_KX_RSA, GNUTLS_KX_DHE_DSS, GNUTLS_KX_DHE_RSA, 0 };
static const int CIPHERS[] = { GNUTLS_CIPHER_AES_256_CBC, GNUTLS_CIPHER_AES_128_CBC, GNUTLS_CIPHER_3DES_CBC, GNUTLS_CIPHER_ARCFOUR_128, 0 };

// FIXME: this clientproxy can only handle 1 SSL connection at the same time
class ClientProxy : public Proxy {
public:
	ClientProxy(const std::string& connect_address, unsigned short connect_port, 
			const std::string& ca_file, bool check_hostname)
	try
		: Proxy(ca_file),
		transport_(connect_address, connect_port, boost::bind(&ClientProxy::on_transport_connect, this, _1)),
		local_address_("127.0.0.1"),
		local_port_(0),
		connect_address_(connect_address),
		acceptor_ptr(),
		check_hostname_(check_hostname)
	{
		for (unsigned short port = MIN_CLIENT_PORT; port <= MAX_CLIENT_PORT; port++) {
			try {
				acceptor_ptr.reset(new asio::ip::tcp::acceptor(transport_.io_service(), asio::ip::tcp::endpoint(asio::ip::address_v4::from_string(local_address_), port)));
				local_port_ = port;
				break;
			} catch (asio::system_error& se) {
				if (port == MAX_CLIENT_PORT)
					throw se;
				if (se.code() != asio::error::address_in_use)
					throw se;
				// this port is already in use, try another one
				continue;
			}
		}
		transport_.connect();
	} catch (asio::system_error& se) {
		throw Exception(std::string(TRANSPORT_ERROR) + se.what());
	}

	const std::string& local_address() const {
		return local_address_;
	}

	unsigned short local_port() const {
		return local_port_;
	}

private:
	Transport& transport() {
		return static_cast<Transport&>(transport_);
	}

	void on_transport_connect(socket_ptr_t remote_socket_ptr) {
		session_ptr_t session_ptr = setup_tls_session(remote_socket_ptr);
		if (!session_ptr) {
			disconnect_(session_ptr_t(), socket_ptr_t(), remote_socket_ptr);
			throw Exception(TLS_SETUP_ERROR);
		}
		
		// start accepting connections on the local socket
		socket_ptr_t local_socket_ptr(new asio::ip::tcp::socket(transport_.io_service()));
		acceptor_ptr->async_accept(*local_socket_ptr, boost::bind(&ClientProxy::on_client_connect, this, 
				asio::placeholders::error, session_ptr, local_socket_ptr, remote_socket_ptr));
	}

	void on_client_connect(const asio::error_code& error, 
			session_ptr_t session_ptr, socket_ptr_t local_socket_ptr, socket_ptr_t remote_socket_ptr) {
		if (error) {
			disconnect_(session_ptr, local_socket_ptr, remote_socket_ptr);
			return;
		}
		tunnel(session_ptr, local_socket_ptr, remote_socket_ptr);
	}

	session_ptr_t setup_tls_session(socket_ptr_t remote_socket_ptr) {
		session_ptr_t session_ptr(new gnutls_session_t(), _SessionPtrDestuctor());
	
		// setup session
		return_val_if_neg(gnutls_init(session_ptr.get(), GNUTLS_CLIENT), session_ptr_t());
		return_val_if_neg(gnutls_set_default_priority(*session_ptr), session_ptr_t());
		return_val_if_neg(gnutls_kx_set_priority(*session_ptr,PRIORITIES), session_ptr_t());
		return_val_if_neg(gnutls_cipher_set_priority(*session_ptr,CIPHERS), session_ptr_t());
		return_val_if_neg(gnutls_credentials_set(*session_ptr, GNUTLS_CRD_CERTIFICATE, x509cred), session_ptr_t());

		// setup transport
		gnutls_transport_set_pull_function(*session_ptr,tls_tunnel::read);
		gnutls_transport_set_push_function(*session_ptr,tls_tunnel::write);
		gnutls_transport_set_ptr(*session_ptr, remote_socket_ptr.get());	
	
		// handshake	
		return_val_if_neg(gnutls_handshake(*session_ptr), session_ptr_t());

		// verify peer
		unsigned int status;
		if (gnutls_certificate_verify_peers2(*session_ptr, &status) != 0)
			throw Exception(TLS_VERIFY_PEER_ERROR);

		gnutls_x509_crt cert;
		const gnutls_datum* cert_list;
		unsigned int cert_list_size;

		// check hostname
		return_val_if_neg(gnutls_x509_crt_init(&cert), session_ptr_t());
		cert_list = gnutls_certificate_get_peers(*session_ptr, &cert_list_size);
		if (!cert_list)
			throw Exception(TLS_CANT_GET_PEER_CERT_ERROR);

		return_val_if_neg(gnutls_x509_crt_import(cert, &cert_list[0], GNUTLS_X509_FMT_DER), session_ptr_t());
		char name[256] = {0};
		size_t namesize = sizeof(name);
		return_val_if_neg(gnutls_x509_crt_get_dn(cert, name, &namesize), session_ptr_t());
		if (check_hostname_ && gnutls_x509_crt_check_hostname(cert, connect_address_.c_str()) == 0)
			throw Exception(TLS_VERIFY_HOSTNAME_ERROR);
		
		return session_ptr;
	}

	ClientTransport transport_;
	std::string local_address_;
	unsigned short local_port_;
	std::string connect_address_;
	boost::shared_ptr<asio::ip::tcp::acceptor> acceptor_ptr;
	bool check_hostname_;
};

class ServerProxy : public Proxy {
public:
	ServerProxy(const std::string& bind_ip, unsigned short bind_port, unsigned short local_port,
			const std::string& ca_file, const std::string& cert_file, const std::string& key_file)
	try
		: Proxy(ca_file),
		transport_(bind_ip, bind_port, boost::bind(&ServerProxy::on_transport_connect, this, _1)),
		local_port_(local_port)
	{
		// setup tls server state
		if (gnutls_certificate_set_x509_key_file (x509cred, cert_file.c_str(), key_file.c_str(), GNUTLS_X509_FMT_PEM) < 0)
			throw Exception(TLS_SETUP_ERROR);

		if (gnutls_dh_params_init(&dh_params) < 0)
			throw Exception(TLS_SETUP_ERROR);

		if (gnutls_dh_params_generate2(dh_params, 1024) < 0)
			throw Exception(TLS_SETUP_ERROR);

		gnutls_certificate_set_dh_params(x509cred, dh_params);
		
		// start accepting connections
		transport_.accept();
	} catch (asio::system_error& se) {
		throw Exception(std::string(TRANSPORT_ERROR) + se.what());
	}
	
private:
	Transport& transport() {
		return static_cast<Transport&>(transport_);
	}

	void on_transport_connect(socket_ptr_t remote_socket_ptr) {
		session_ptr_t session_ptr = setup_tls_session(remote_socket_ptr);
		if (!session_ptr) {
			disconnect_(session_ptr_t(), socket_ptr_t(), remote_socket_ptr);
			return;
		}
		
		socket_ptr_t local_socket_ptr(new asio::ip::tcp::socket(transport_.io_service()));
		try {
			asio::ip::tcp::resolver resolver(transport_.io_service());
			asio::ip::tcp::resolver::query query("127.0.0.1", boost::lexical_cast<std::string>(local_port_));
			asio::ip::tcp::resolver::iterator iterator(resolver.resolve(query));		
			local_socket_ptr->connect(*iterator);		
		} catch (asio::system_error& se) {
			disconnect_(session_ptr, local_socket_ptr, remote_socket_ptr);
			return;
		}

		tunnel(session_ptr, local_socket_ptr, remote_socket_ptr);
	}

	session_ptr_t setup_tls_session(socket_ptr_t remote_socket_ptr) {
		session_ptr_t session_ptr(new gnutls_session_t());
	
		// setup session
		return_val_if_neg(gnutls_init(session_ptr.get(), GNUTLS_SERVER), session_ptr_t());
		return_val_if_neg(gnutls_set_default_priority(*session_ptr), session_ptr_t());
		return_val_if_neg(gnutls_credentials_set(*session_ptr, GNUTLS_CRD_CERTIFICATE, x509cred), session_ptr_t());
		gnutls_certificate_server_set_request(*session_ptr,GNUTLS_CERT_REQUEST);
		gnutls_dh_set_prime_bits(*session_ptr, 1024);

		// setup ssl transport
		gnutls_transport_set_pull_function(*session_ptr, tls_tunnel::read);
		gnutls_transport_set_push_function(*session_ptr, tls_tunnel::write);
		gnutls_transport_set_ptr(*session_ptr, remote_socket_ptr.get());	
	
		// execute ssl handshake
		gnutls_certificate_server_set_request(*session_ptr, GNUTLS_CERT_REQUEST);
		return_val_if_neg(gnutls_handshake(*session_ptr), session_ptr_t());

		return session_ptr;
	}
	
	ServerTransport transport_;
	unsigned short local_port_;

	gnutls_dh_params_t dh_params;
};

} /* namespace tls_tunnel */

#endif /* __TLS_TUNNEL_H__ */