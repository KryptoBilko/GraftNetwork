/**
@file
@author from CrypoNote (see copyright below; Andrey N. Sabelnikov)
@monero rfree
@brief the connection templated-class for one peer connection
*/
// Copyright (c) 2006-2013, Andrey N. Sabelnikov, www.sabelnikov.net
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
// * Neither the name of the Andrey N. Sabelnikov nor the
// names of its contributors may be used to endorse or promote products
// derived from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER  BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 



#ifndef _ABSTRACT_TCP_SERVER2_H_ 
#define _ABSTRACT_TCP_SERVER2_H_ 

#include "async_state_machine.h"

#include <string>
#include <vector>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <atomic>
#include <cassert>
#include <map>
#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/array.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/functional.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/interprocess/detail/atomic.hpp>
#include <boost/thread/thread.hpp>
#include "net_utils_base.h"
#include "syncobj.h"
#include "connection_basic.hpp"
#include "network_throttle-detail.hpp"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "net"

#define ABSTRACT_SERVER_SEND_QUE_MAX_COUNT (1024)

namespace epee
{
namespace net_utils
{

  using async_state_machine=cblp::async_callback_state_machine;

  struct i_connection_filter
  {
    virtual bool is_remote_host_allowed(const epee::net_utils::network_address &address)=0;
  protected:
    virtual ~i_connection_filter(){}
  };
  

  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  /// Represents a single connection from a client.
//  template<class t_protocol_handler>
//  struct do_send_state_machine<t_protocol_handler>;
//  template<class t_protocol_handler>
//  struct do_send_chunk_state_machine<t_protocol_handler>;
  template<class t_protocol_handler2> struct do_send_chunk_state_machine;


  template<class t_protocol_handler>
  class connection
    : public boost::enable_shared_from_this<connection<t_protocol_handler> >,
    private boost::noncopyable, 
    public i_service_endpoint,
    public connection_basic
  {
    template<class t_protocol_handler2> friend struct do_send_chunk_state_machine;

//    typedef boost::function<void(const boost::system::error_code&)> Type1;
//    typedef boost::shared_ptr<Type1> callback_type;
    typedef boost::shared_ptr<boost::function<void(const boost::system::error_code&)>> callback_type;

  public:
    typedef typename t_protocol_handler::connection_context t_connection_context;

    struct shared_state : connection_basic_shared_state
    {
      shared_state()
        : connection_basic_shared_state(), pfilter(nullptr), config(), stop_signal_sent(false)
      {}

      i_connection_filter* pfilter;
      typename t_protocol_handler::config_type config;
      bool stop_signal_sent;
    };

    /// Construct a connection with the given io_service.
    explicit connection( boost::asio::io_service& io_service,
                        boost::shared_ptr<shared_state> state,
			t_connection_type connection_type,
			epee::net_utils::ssl_support_t ssl_support);

    explicit connection( boost::asio::ip::tcp::socket&& sock,
			 boost::shared_ptr<shared_state> state,
			t_connection_type connection_type,
			epee::net_utils::ssl_support_t ssl_support);



    virtual ~connection() noexcept(false);

    /// Start the first asynchronous operation for the connection.
    bool start(bool is_income, bool is_multithreaded);

    // `real_remote` is the actual endpoint (if connection is to proxy, etc.)
    bool start(bool is_income, bool is_multithreaded, network_address real_remote);

    void get_context(t_connection_context& context_){context_ = context;}

    void call_back_starter();
    
    void save_dbg_log();


		bool speed_limit_is_enabled() const; ///< tells us should we be sleeping here (e.g. do not sleep on RPC connections)

    bool cancel();
    
  private:
    //----------------- i_service_endpoint ---------------------
    virtual bool do_send(const void* ptr, size_t cb); ///< (see do_send from i_service_endpoint)
    virtual bool do_send_chunk(const void* ptr, size_t cb); ///< will send (or queue) a part of data
    virtual bool send_done();
    virtual bool close();
    virtual bool call_run_once_service_io();
    virtual bool request_callback();
    virtual boost::asio::io_service& get_io_service();
    virtual bool add_ref();
    virtual bool release();
    //------------------------------------------------------
    boost::shared_ptr<connection<t_protocol_handler> > safe_shared_from_this();
    bool shutdown();
    /// Handle completion of a receive operation.
    void handle_receive(const boost::system::error_code& e,
      std::size_t bytes_transferred);

    /// Handle completion of a read operation.
    void handle_read(const boost::system::error_code& e,
      std::size_t bytes_transferred);

    /// Handle completion of a write operation.
    void handle_write(const boost::system::error_code& e, size_t cb);
    void handle_write_after_delay1(const boost::system::error_code& e, size_t bytes_sent);
    void handle_write_after_delay2(const boost::system::error_code& e, size_t bytes_sent);


    /// reset connection timeout timer and callback
    void reset_timer(boost::posix_time::milliseconds ms, bool add);
    boost::posix_time::milliseconds get_default_timeout();
    boost::posix_time::milliseconds get_timeout_from_bytes_read(size_t bytes);

    /// host connection count tracking
    unsigned int host_count(const std::string &host, int delta = 0);

    /// Buffer for incoming data.
    boost::array<char, 8192> buffer_;
    size_t buffer_ssl_init_fill;

    t_connection_context context;

	// TODO what do they mean about wait on destructor?? --rfree :
    //this should be the last one, because it could be wait on destructor, while other activities possible on other threads
    t_protocol_handler m_protocol_handler;
    //typename t_protocol_handler::config_type m_dummy_config;
    size_t m_reference_count = 0; // reference count managed through add_ref/release support
    boost::shared_ptr<connection<t_protocol_handler> > m_self_ref; // the reference to hold
    critical_section m_self_refs_lock;
    critical_section m_chunking_lock; // held while we add small chunks of the big do_send() to small do_send_chunk()
    critical_section m_shutdown_lock; // held while shutting down
    
    t_connection_type m_connection_type;
    
    // for calculate speed (last 60 sec)
    network_throttle m_throttle_speed_in;
    network_throttle m_throttle_speed_out;
    boost::mutex m_throttle_speed_in_mutex;
    boost::mutex m_throttle_speed_out_mutex;

    boost::asio::deadline_timer m_timer;
    bool m_local;
    bool m_ready_to_close;
    std::string m_host;
    std::list<std::pair<int64_t, callback_type>> on_write_callback_list;

  public:
    void setRpcStation();
    bool add_on_write_callback(std::pair<int64_t, callback_type> &callback)
    {
        if (!m_send_que_lock.tryLock())
            return false;
        int64_t bytes_in_que = 0;
        for (auto entry : m_send_que)
            bytes_in_que += entry.size();

        int64_t bytes_to_wait = bytes_in_que + callback.first;

        for (auto entry : on_write_callback_list)
            bytes_to_wait -= entry.first;

        if (bytes_to_wait <= 0) {
            m_send_que_lock.unlock();
            return false;
        }

        callback.first = bytes_to_wait;
        on_write_callback_list.push_back(callback);
        m_send_que_lock.unlock();
        return true;

    }
  };


  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  template<class t_protocol_handler>
  class boosted_tcp_server
    : private boost::noncopyable
  {
    enum try_connect_result_t
    {
      CONNECT_SUCCESS,
      CONNECT_FAILURE,
      CONNECT_NO_SSL,
    };

  public:
    typedef boost::shared_ptr<connection<t_protocol_handler> > connection_ptr;
    typedef typename t_protocol_handler::connection_context t_connection_context;
    /// Construct the server to listen on the specified TCP address and port, and
    /// serve up files from the given directory.

    boosted_tcp_server(t_connection_type connection_type);
    explicit boosted_tcp_server(boost::asio::io_service& external_io_service, t_connection_type connection_type);
    ~boosted_tcp_server();
    
    std::map<std::string, t_connection_type> server_type_map;
    void create_server_type_map();

    bool init_server(uint32_t port, const std::string address = "0.0.0.0", ssl_options_t ssl_options = ssl_support_t::e_ssl_support_autodetect);
    bool init_server(const std::string port,  const std::string& address = "0.0.0.0", ssl_options_t ssl_options = ssl_support_t::e_ssl_support_autodetect);

    /// Run the server's io_service loop.
    bool run_server(size_t threads_count, bool wait = true, const boost::thread::attributes& attrs = boost::thread::attributes());

    /// wait for service workers stop
    bool timed_wait_server_stop(uint64_t wait_mseconds);

    /// Stop the server.
    void send_stop_signal();

    bool is_stop_signal_sent() const noexcept { return m_stop_signal_sent; };

    const std::atomic<bool>& get_stop_signal() const noexcept { return m_stop_signal_sent; }

    void set_threads_prefix(const std::string& prefix_name);

    bool deinit_server(){return true;}

    size_t get_threads_count(){return m_threads_count;}

    void set_connection_filter(i_connection_filter* pfilter);

    void set_default_remote(epee::net_utils::network_address remote)
    {
      default_remote = std::move(remote);
    }

    bool add_connection(t_connection_context& out, boost::asio::ip::tcp::socket&& sock, network_address real_remote, epee::net_utils::ssl_support_t ssl_support = epee::net_utils::ssl_support_t::e_ssl_support_autodetect);
    try_connect_result_t try_connect(connection_ptr new_connection_l, const std::string& adr, const std::string& port, boost::asio::ip::tcp::socket &sock_, const boost::asio::ip::tcp::endpoint &remote_endpoint, const std::string &bind_ip, uint32_t conn_timeout, epee::net_utils::ssl_support_t ssl_support);
    bool connect(const std::string& adr, const std::string& port, uint32_t conn_timeot, t_connection_context& cn, const std::string& bind_ip = "0.0.0.0", epee::net_utils::ssl_support_t ssl_support = epee::net_utils::ssl_support_t::e_ssl_support_autodetect);
    template<class t_callback>
    bool connect_async(const std::string& adr, const std::string& port, uint32_t conn_timeot, const t_callback &cb, const std::string& bind_ip = "0.0.0.0", epee::net_utils::ssl_support_t ssl_support = epee::net_utils::ssl_support_t::e_ssl_support_autodetect);

    typename t_protocol_handler::config_type& get_config_object()
    {
      assert(m_state != nullptr); // always set in constructor
      return m_state->config;
    }

    int get_binded_port(){return m_port;}

    long get_connections_count() const
    {
      assert(m_state != nullptr); // always set in constructor
      auto connections_count = m_state->sock_count > 0 ? (m_state->sock_count - 1) : 0; // Socket count minus listening socket
      return connections_count;
    }

    boost::asio::io_service& get_io_service(){return io_service_;}

    struct idle_callback_conext_base
    {
      virtual ~idle_callback_conext_base(){}

      virtual bool call_handler(){return true;}

      idle_callback_conext_base(boost::asio::io_service& io_serice):
                                                          m_timer(io_serice)
      {}
      boost::asio::deadline_timer m_timer;
    };

    template <class t_handler>
    struct idle_callback_conext: public idle_callback_conext_base
    {
      idle_callback_conext(boost::asio::io_service& io_serice, t_handler& h, uint64_t period):
                                                    idle_callback_conext_base(io_serice),
                                                    m_handler(h)
      {this->m_period = period;}

      t_handler m_handler;
      virtual bool call_handler()
      {
        return m_handler();
      }
      uint64_t m_period;
    };

    template<class t_handler>
    bool add_idle_handler(t_handler t_callback, uint64_t timeout_ms)
      {
        boost::shared_ptr<idle_callback_conext<t_handler>> ptr(new idle_callback_conext<t_handler>(io_service_, t_callback, timeout_ms));
        //needed call handler here ?...
        ptr->m_timer.expires_from_now(boost::posix_time::milliseconds(ptr->m_period));
        ptr->m_timer.async_wait(boost::bind(&boosted_tcp_server<t_protocol_handler>::global_timer_handler<t_handler>, this, ptr));
        return true;
      }

    template<class t_handler>
    bool global_timer_handler(/*const boost::system::error_code& err, */boost::shared_ptr<idle_callback_conext<t_handler>> ptr)
    {
      //if handler return false - he don't want to be called anymore
      if(!ptr->call_handler())
        return true;
      ptr->m_timer.expires_from_now(boost::posix_time::milliseconds(ptr->m_period));
      ptr->m_timer.async_wait(boost::bind(&boosted_tcp_server<t_protocol_handler>::global_timer_handler<t_handler>, this, ptr));
      return true;
    }

    template<class t_handler>
    bool async_call(t_handler t_callback)
    {
      io_service_.post(t_callback);
      return true;
    }

  private:
    /// Run the server's io_service loop.
    bool worker_thread();
    /// Handle completion of an asynchronous accept operation.
    void handle_accept(const boost::system::error_code& e);

    bool is_thread_worker();

    const boost::shared_ptr<typename connection<t_protocol_handler>::shared_state> m_state;

    /// The io_service used to perform asynchronous operations.
    struct worker
    {
      worker()
        : io_service(), work(io_service)
      {}

      boost::asio::io_service io_service;
      boost::asio::io_service::work work;
    };
    std::unique_ptr<worker> m_io_service_local_instance;
    boost::asio::io_service& io_service_;    

    /// Acceptor used to listen for incoming connections.
    boost::asio::ip::tcp::acceptor acceptor_;
    epee::net_utils::network_address default_remote;

    std::atomic<bool> m_stop_signal_sent;
    uint32_t m_port;
    std::string m_address;
    std::string m_thread_name_prefix; //TODO: change to enum server_type, now used
    size_t m_threads_count;
    std::vector<boost::shared_ptr<boost::thread> > m_threads;
    boost::thread::id m_main_thread_id;
    critical_section m_threads_lock;
    volatile uint32_t m_thread_index; // TODO change to std::atomic

    t_connection_type m_connection_type;

    /// The next connection to be accepted
    connection_ptr new_connection_;

    boost::mutex connections_mutex;
    std::set<connection_ptr> connections_;
  }; // class <>boosted_tcp_server


  template<class t_protocol_handler>
  struct do_send_chunk_state_machine  : protected async_state_machine
  {
    static boost::shared_ptr<async_state_machine> create(boost::asio::io_service &io_service
                                                  , int64_t timeout
                                                  , async_state_machine::callback_type finalizer
                                                  , boost::weak_ptr<connection<t_protocol_handler>>& conn
                                                  , const void* message
                                                  , size_t msg_len
                                                  )
    {
      boost::shared_ptr<async_callback_state_machine> ret(
            new do_send_chunk_state_machine(io_service, timeout, finalizer, conn, message, msg_len)
            );

      return ret;
    }

    void send_result(const boost::system::error_code& ec)
    {
      if (ec) {
        stop(call_result_type::failed);
      }
      else {
        stop(call_result_type::succesed);
      }
    }

  private:
    template<class t_protocol_handler2> friend struct connection_write_task;

    struct connection_write_task : public i_task
    {
      connection_write_task(boost::shared_ptr<async_state_machine> machine)
        : machine(machine)
      {}

      template<class t_protocol_handler2>
      /*virtual*/ void exec()
      {
        boost::shared_ptr<do_send_chunk_state_machine> mach
                = boost::dynamic_pointer_cast<do_send_chunk_state_machine<t_protocol_handler2>>(machine);
        boost::shared_ptr<connection<t_protocol_handler2>> con_ = mach->conn;
        con_->m_send_que_lock.lock(); // *** critical ***
        epee::misc_utils::auto_scope_leave_caller scope_exit_handler = epee::misc_utils::create_scope_leave_handler([&](){con_->m_send_que_lock.unlock();});

        con_->m_send_que.resize(con_->m_send_que.size()+1);
        con_->m_send_que.back().assign((const char*)mach->message, mach->length);
        typename connection<t_protocol_handler>::callback_type callback = boost::bind(&do_send_chunk_state_machine::send_result,mach,_1);
        con_->add_on_write_callback(std::pair<int64_t, typename connection<t_protocol_handler>::callback_type> { mach->length, callback } );

        if(con_->m_send_que.size() == 1) {
          // no active operation
          auto size_now = con_->m_send_que.front().size();
          boost::asio::async_write(con_->socket_, boost::asio::buffer(con_->m_send_que.front().data(), size_now ) ,
                                   boost::bind(&connection<t_protocol_handler>::handle_write, con_, _1, _2)
                                   );
        }
      }

      boost::shared_ptr<async_state_machine> machine;
    };


    do_send_chunk_state_machine(boost::asio::io_service &io_service
                                , int64_t timeout
                                , async_state_machine::callback_type caller
                                , boost::weak_ptr<connection<t_protocol_handler>>& conn
                                , const void* message
                                , size_t msg_len
                                )
      : async_state_machine(io_service, timeout, caller)
      , conn(conn)
      , message(const_cast<void*>(message))
      , length(msg_len)
    {
    }

    /*virtual*/ bool start()
    {
      try {
        boost::shared_ptr<async_state_machine> self;
        try {
          self = async_state_machine::shared_from_this();
        }
        catch (boost::bad_weak_ptr& ex) {
          return false;
        }
        catch (...) {
          return false;
        }

        if(conn->m_was_shutdown)
          return false;

        do {
          CRITICAL_REGION_LOCAL(conn->m_throttle_speed_out_mutex);
          conn->m_throttle_speed_out.handle_trafic_exact(length);
          conn->context.m_current_speed_up = conn->m_throttle_speed_out.get_current_speed();
        } while(0);

        conn->context.m_last_send = time(NULL);
        conn->context.m_send_cnt += length;


        boost::shared_ptr<connection_write_task> send_task(self);

        if (conn->speed_limit_is_enabled()) {
          int64_t delay = conn->sleep_before_packet(length);
          schedule_task(send_task, delay);
        }
        else {
            schedule_task(send_task);
        }

        return true;
      }
      catch (std::exception& ex) {
        (void) ex;
        return false;
      }
      catch (...) {
        return false;
      }
    }


    boost::shared_ptr<connection<t_protocol_handler>> conn;
    void * message;
    size_t length;
  }; // do_send_chunk_state_machine


  template<class t_protocol_handler>
  using do_send_state_machine = do_send_chunk_state_machine<t_protocol_handler>;


} // namespace
} // namespace

#include "abstract_tcp_server2.inl"

#endif
