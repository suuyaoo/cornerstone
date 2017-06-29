/**
*
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  The ASF licenses
* this file to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "cornerstone.hxx"
#include <iostream>
#include <cassert>
#include <string>

using namespace cornerstone;

#ifdef _WIN32
#define LOG_INDEX_FILE "\\store.idx"
#define LOG_DATA_FILE "\\store.dat"
#define LOG_START_INDEX_FILE "\\store.sti"
#define LOG_INDEX_FILE_BAK "\\store.idx.bak"
#define LOG_DATA_FILE_BAK "\\store.dat.bak"
#define LOG_START_INDEX_FILE_BAK "\\store.sti.bak"

#include <Windows.h>

int mkdir(const char* path, int mode) {
    (void)mode;
    return 1 == ::CreateDirectoryA(path, NULL) ? 0 : -1;
}

int rmdir(const char* path) {
    return 1 == ::RemoveDirectoryA(path) ? 0 : -1;
}
#undef min
#undef max
#else
#define LOG_INDEX_FILE "/store.idx"
#define LOG_DATA_FILE "/store.dat"
#define LOG_START_INDEX_FILE "/store.sti"
#define LOG_INDEX_FILE_BAK "/store.idx.bak"
#define LOG_DATA_FILE_BAK "/store.dat.bak"
#define LOG_START_INDEX_FILE_BAK "/store.sti.bak"

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

void cleanup(const std::string& folder) {
    std::remove((folder + LOG_INDEX_FILE).c_str());
    std::remove((folder + LOG_DATA_FILE).c_str());
    std::remove((folder + LOG_START_INDEX_FILE).c_str());
    std::remove((folder + LOG_INDEX_FILE_BAK).c_str());
    std::remove((folder + LOG_DATA_FILE_BAK).c_str());
    std::remove((folder + LOG_START_INDEX_FILE_BAK).c_str());
}

void cleanup() {
    cleanup(".");
}

ptr<asio_service> asio_svc_;
std::condition_variable stop_cv1;
std::mutex lock1;
std::mutex stop_test_lock1;
std::condition_variable stop_test_cv1;

class simple_state_mgr: public state_mgr{
public:
    simple_state_mgr(int32 srv_id)
        : srv_id_(srv_id) {
        store_path_ = sstrfmt("store%d").fmt(srv_id_);
        conf_ = cs_new<cluster_config>();
        conf_->get_servers().push_back(cs_new<srv_config>(srv_id, sstrfmt("tcp://127.0.0.1:900%d").fmt(srv_id)));
    }

public:
    virtual ptr<cluster_config> load_config() {
        return conf_;
    }

    virtual void save_config(const cluster_config& config)
    {
        std::cout << "save config " << srv_id_ << std::endl;
        ptr<buffer> bf = config.serialize();
        conf_ = cluster_config::deserialize(*bf);
    }
    
    virtual void save_state(const srv_state& state) {}
    virtual ptr<srv_state> read_state() {
        return cs_new<srv_state>();
    }

    virtual ptr<log_store> load_log_store() {
        mkdir(store_path_.c_str(), 0766);
        return cs_new<fs_log_store>(store_path_);
    }

    virtual int32 server_id() {
        return srv_id_;
    }

    virtual void system_exit(const int exit_code) {
        std::cout << "system exiting with code " << exit_code << std::endl;
    }

private:
    int32 srv_id_;
    std::string store_path_;
    ptr<cluster_config> conf_;
};

class console_logger : public logger {
public:
    console_logger(const std::string& name) : name_(name) {}

    __nocopy__(console_logger)
public:
    virtual void debug(const std::string& log_line) {
        //printf("%s %s %s\n", "DEBUG", this->name_.c_str(), log_line.c_str());
    }

    virtual void info(const std::string& log_line) {
        printf("%s %s %s\n", "INFO", this->name_.c_str(), log_line.c_str());
    }

    virtual void warn(const std::string& log_line) {
        printf("%s %s %s\n", "WARN", this->name_.c_str(), log_line.c_str());
    }

    virtual void err(const std::string& log_line) {
        printf("%s %s %s\n", "ERROR", this->name_.c_str(), log_line.c_str());
    }

private:
    std::string name_;
};

class echo_state_machine : public state_machine {
public:
    echo_state_machine() : lock_() {}
public:
    virtual void commit(const ulong log_idx, buffer& data) {
        auto_lock(lock_);
        std::cout << "commit message:" << reinterpret_cast<const char*>(data.data()) << std::endl;
    }

    virtual void pre_commit(const ulong log_idx, buffer& data) {
        auto_lock(lock_);
        std::cout << "pre-commit: " << reinterpret_cast<const char*>(data.data()) << std::endl;
    }

    virtual void rollback(const ulong log_idx, buffer& data) {
        auto_lock(lock_);
        std::cout << "rollback: " << reinterpret_cast<const char*>(data.data()) << std::endl;
    }

    virtual void save_snapshot_data(snapshot& s, const ulong offset, buffer& data) {}
    virtual bool apply_snapshot(snapshot& s) {
        return false;
    }

    virtual int read_snapshot_data(snapshot& s, const ulong offset, buffer& data) {
        return 0;
    }

    virtual ptr<snapshot> last_snapshot() {
        return ptr<snapshot>();
    }

    virtual void create_snapshot(snapshot& s, async_result<bool>::handler_type& when_done) {}
private:
    std::mutex lock_;
};


void run_raft_instance_with_asio(int srv_id) {
    //ptr<logger> l(asio_svc_->create_logger(asio_service::log_level::debug, sstrfmt("log%d.log").fmt(srv_id)));
    ptr<logger> l(new console_logger(sstrfmt("log%d.log").fmt(srv_id)));

    ptr<rpc_listener> listener(asio_svc_->create_rpc_listener((ushort)(9000 + srv_id), l));
    ptr<state_mgr> smgr(cs_new<simple_state_mgr>(srv_id));
    ptr<state_machine> smachine(cs_new<echo_state_machine>());
    raft_params* params(new raft_params());
    (*params).with_election_timeout_lower(200)
        .with_election_timeout_upper(400)
        .with_hb_interval(100)
        .with_max_append_size(100)
        .with_rpc_failure_backoff(50);
    ptr<delayed_task_scheduler> scheduler = asio_svc_;
    ptr<rpc_client_factory> rpc_cli_factory = asio_svc_;
    context* ctx(new context(smgr, smachine, listener, l, rpc_cli_factory, scheduler, params));
    ptr<raft_server> server(cs_new<raft_server>(ctx, srv_id == 1));
    
    listener->listen(server);
    
    {
        std::unique_lock<std::mutex> ulock(lock1);
        stop_cv1.wait(ulock);
        listener->stop();
    }
}

void run_raft_client(int srv_id)
{
    std::cout << "waiting for leader election..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ptr<rpc_client> client(asio_svc_->create_client(sstrfmt("tcp://127.0.0.1:900%d").fmt(srv_id)));
    ptr<req_msg> msg = cs_new<req_msg>(0, msg_type::client_request, 0, 1, 0, 0, 0);
    ptr<buffer> buf = buffer::alloc(100);
    buf->put("hello");
    buf->pos(0);
    msg->log_entries().push_back(cs_new<log_entry>(0, buf));
    rpc_handler handler = (rpc_handler)([&client](ptr<resp_msg>& rsp, ptr<rpc_exception>& err) -> void {
        if (err) {
            std::cout << err->what() << std::endl;
        }

        assert(!err);
        assert(rsp->get_accepted() || rsp->get_dst() > 0);
        if (!rsp->get_accepted()) {
            std::cout << " leader is: " << rsp->get_dst() << std::endl;
            client = asio_svc_->create_client(sstrfmt("tcp://127.0.0.1:900%d").fmt(rsp->get_dst()));
            ptr<req_msg> msg = cs_new<req_msg>(0, msg_type::client_request, 0, 1, 0, 0, 0);
            ptr<buffer> buf = buffer::alloc(100);
            buf->put("hello");
            buf->pos(0);
            msg->log_entries().push_back(cs_new<log_entry>(0, buf));
            rpc_handler handler = (rpc_handler)([&client](ptr<resp_msg>& rsp1, ptr<rpc_exception>& err1) -> void {
                assert(rsp1->get_accepted());
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                stop_test_cv1.notify_all();
            });
            client->send(msg, handler);
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            stop_test_cv1.notify_all();
        }
    });

    
    client->send(msg, handler);

    {
        std::unique_lock<std::mutex> l(stop_test_lock1);
        stop_test_cv1.wait(l);
    }

    return;
    
    stop_cv1.notify_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    asio_svc_->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::remove("log1.log");
    std::remove("log2.log");
    std::remove("log3.log");
    cleanup("store1");
    cleanup("store2");
    cleanup("store3");
    rmdir("store1");
    rmdir("store2");
    rmdir("store3");
}

void config_cluster(cluster_config& config)
{
    ptr<rpc_client> client(asio_svc_->create_client("tcp://127.0.0.1:9001"));
    std::list<ptr<srv_config>>& srvs(config.get_servers());
    for (cluster_config::srv_itor it = srvs.begin(); it != srvs.end(); ++it) {
        ptr<buffer> buf((*it)->serialize());
        ptr<log_entry> log(cs_new<log_entry>(0, buf, log_val_type::cluster_server));
        ptr<req_msg> req(cs_new<req_msg>((ulong)0, msg_type::add_server_request, 0, 0, (ulong)0, (ulong)0, (ulong)0));
        req->log_entries().push_back(log);
        
        rpc_handler handler = (rpc_handler)([&client](ptr<resp_msg>& rsp, ptr<rpc_exception>& err) -> void {
            if (err) {
                std::cout << err->what() << std::endl;
            }

            assert(!err);
            assert(rsp->get_accepted() || rsp->get_dst() > 0);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            stop_test_cv1.notify_all();
        });

        client->send(req, handler);
        
        {
            std::unique_lock<std::mutex> l(stop_test_lock1);
            stop_test_cv1.wait(l);
        }
    }
}

int main(int argc, char** argv)
{
    asio_svc_ = cs_new<asio_service>();
    if (argc > 2) {
        std::string type = argv[1];
        if (type == "server") {
            std::string svcid = argv[2];
            if (svcid == "1") {
                run_raft_instance_with_asio(1);
            }
            else if (svcid == "2") {
                run_raft_instance_with_asio(2);
            }
            else if (svcid == "3") {
                run_raft_instance_with_asio(3);
            }
        }
        else if (type == "client") {
            std::string svcid = argv[2];
            if (svcid == "1") {
                run_raft_client(1);
            }
            else if (svcid == "2") {
                run_raft_client(2);
            }
            else if (svcid == "3") {
                run_raft_client(3);
            }
        }
        else if (type == "config") {
            ptr<cluster_config> conf = cs_new<cluster_config>();
            conf->get_servers().push_back(cs_new<srv_config>(1, "tcp://127.0.0.1:9001"));
            conf->get_servers().push_back(cs_new<srv_config>(2, "tcp://127.0.0.1:9002"));
            conf->get_servers().push_back(cs_new<srv_config>(3, "tcp://127.0.0.1:9003"));
            config_cluster(*conf);
        }
    }

    return 0;
}