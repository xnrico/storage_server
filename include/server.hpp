#pragma once

#include <evhttp.h>
#include <string>
#include <vector>
#include "data_manager.hpp"

namespace ricox {

class server final {
   private:
	uint16_t server_port;
	std::string server_ip;
	std::string download_url_prefix;

	// Main callback functions
	static auto generic_callback(evhttp_request* req, void* arg) -> void;
	static auto download(evhttp_request* req, void* arg) -> void;
	static auto upload(evhttp_request* req, void* arg) -> void;
	static auto show(evhttp_request* req, void* arg) -> void;

	// Helper functions
	static auto generate_file_list(const std::vector<storage_info>& files) -> std::string;
	static auto format_size(uint64_t bytes) -> std::string;
	static auto get_etag(const storage_info& info) -> std::string;

   public:
	server();
	~server() = default;

	auto start_server() -> bool;
};
}  // namespace ricox